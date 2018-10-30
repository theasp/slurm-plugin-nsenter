#define _GNU_SOURCE
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <pwd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <mntent.h>
#include <slurm/spank.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>


/*  SPANK plugin operations. SPANK plugin should have at least one of
 *   these functions defined non-NULL.
 *
 *  Plug-in callbacks are completed at the following points in slurmd:
 *
 *   slurmd
 *        `-> slurmd_init()
 *        |
 *        `-> job_prolog()
 *        |
 *        | `-> slurmstepd
 *        |      `-> init ()
 *        |       -> process spank options
 *        |       -> init_post_opt ()
 *        |      + drop privileges (initgroups(), seteuid(), chdir())
 *        |      `-> user_init ()
 *        |      + for each task
 *        |      |       + fork ()
 *        |      |       |
 *        |      |       + reclaim privileges
 *        |      |       `-> task_init_privileged ()
 *        |      |       |
 *        |      |       + become_user ()
 *        |      |       `-> task_init ()
 *        |      |       |
 *        |      |       + execve ()
 *        |      |
 *        |      + reclaim privileges
 *        |      + for each task
 *        |      |     `-> task_post_fork ()
 *        |      |
 *        |      + for each task
 *        |      |       + wait ()
 *        |      |          `-> task_exit ()
 *        |      `-> exit ()
 *        |
 *        `---> job_epilog()
 *        |
 *        `-> slurmd_exit()
 *
 *   In srun only the init(), init_post_opt() and local_user_init(), and exit()
 *    callbacks are used.
 *
 *   In sbatch/salloc only the init(), init_post_opt(), and exit() callbacks
 *    are used.
 *
 *   In slurmd proper, only the slurmd_init(), slurmd_exit(), and
 *    job_prolog/epilog callbacks are used.
 *
 */

SPANK_PLUGIN(task_nsenter, 1);

#define PLUGIN_NAME "task_nsenter"
#define TASK_NSENTER_DIR "/var/run/slurm-llnl/task_nsenter"

static char cwd_path[PATH_MAX];
static int init = 0;
static int root_fd = 0;
#ifdef CLONE_NEWCGROUP
static int cgroup_fd = 0;
#endif
static int ipc_fd = 0;
static int uts_fd = 0;
static int net_fd = 0;
static int pid_fd = 0;
static int mnt_fd = 0;
static int user_fd = 0;

static ino_t root_ino = 0;
#ifdef CLONE_NEWCGROUP
static ino_t cgroup_ino = 0;
#endif
static ino_t ipc_ino = 0;
static ino_t uts_ino = 0;
static ino_t net_ino = 0;
static ino_t pid_ino = 0;
static ino_t mnt_ino = 0;
static ino_t user_ino = 0;

static int _get_ino(const char *path, ino_t *ino) {
  struct stat st;

  if (stat(path, &st) != 0)
    return -1;

  *ino = st.st_ino;
  return 0;
}

int _open (int *fd, ino_t *ino, char *dir, char *sub) {
  char path[PATH_MAX];
  struct stat st;

  snprintf(path, PATH_MAX, "%s/%s", dir, sub);

  *fd = open(path, O_RDONLY);
  if (*fd == -1) {
    slurm_error("%s: Unable to open %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  if (stat(path, &st) != 0) {
    slurm_error("%s: Unable to stat %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  *ino = st.st_ino;

  slurm_debug("%s: Path %s is fd %d and inode %lu", PLUGIN_NAME, path, *fd, *ino);

  return 0;
}

/* Taken from util-linux/nsenter.c */
static void _continue_as_child (void) {
  pid_t child = fork();
  int status;
  pid_t ret;

  if (child < 0) {
    slurm_error("%s: Fork failed: %s", PLUGIN_NAME, strerror(errno));
    return;
  }

  /* Only the child returns */
  if (child == 0)
    return;

  for (;;) {
    ret = waitpid(child, &status, WUNTRACED);
    if ((ret == child) && (WIFSTOPPED(status))) {
      /* The child suspended so suspend us as well */
      kill(getpid(), SIGSTOP);
      kill(child, SIGCONT);
    } else {
      break;
    }
  }
  /* Return the child's exit code if possible */
  if (WIFEXITED(status)) {
    exit(WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    kill(getpid(), WTERMSIG(status));
  }
  exit(EXIT_FAILURE);
}

int _open_job (uint32_t job_id) {
  int errs = 0;
  char dir[PATH_MAX];

  snprintf(dir, PATH_MAX, "%s/%d", TASK_NSENTER_DIR, job_id);

  if (getcwd(cwd_path, sizeof(cwd_path)) == NULL) {
    slurm_error("%s: Unable to get current working directory: %s", PLUGIN_NAME, strerror(errno));
    return -1;
  }

  errs += _open(&root_fd, &root_ino, dir, "root");
#ifdef CLONE_NEWCGROUP
  errs += _open(&cgroup_fd, &cgroup_ino, dir, "ns/cgroup");
#endif
  errs += _open(&ipc_fd, &ipc_ino, dir, "ns/ipc");
  errs += _open(&uts_fd, &uts_ino, dir, "ns/uts");
  errs += _open(&net_fd, &net_ino, dir, "ns/net");
  errs += _open(&pid_fd, &pid_ino, dir, "ns/pid");
  errs += _open(&mnt_fd, &mnt_ino, dir, "ns/mnt");
  errs += _open(&user_fd, &user_ino, dir, "ns/user");

  init = 1;
  return errs == 0 ? 0 : -1;
}

int _setns (int nstype, char *nstype_name, int fd, ino_t ino) {
  char path[PATH_MAX];
  ino_t cur_ino;

  snprintf(path, PATH_MAX, "/proc/self/ns/%s", nstype_name);
  if (_get_ino(path, &cur_ino) != 0) {
    slurm_error("%s: Unable to get current inode for %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  if (ino == cur_ino) {
    slurm_debug("%s: Already in same %s namespace (%lu)", PLUGIN_NAME, nstype_name, ino);
    return 0;
  }

  slurm_debug("%s: Using %s namespace from fd %d (%lu)", PLUGIN_NAME, nstype_name, fd, ino);

  if (setns(fd, nstype) == -1) {
    slurm_error("%s: Unable to set namespace: %s", PLUGIN_NAME, strerror(errno));
    return -1;
  }

  return 0;
}

int _nsenter () {
  int errs = 0;
  int user_err = 0;

  if (init != 1) {
    slurm_error("%s: Not initialized", PLUGIN_NAME);
    return -1;
  }

  user_err = _setns(CLONE_NEWUSER, "user", user_fd, user_ino);
#ifdef CLONE_NEWCGROUP
  errs += _setns(CLONE_NEWCGROUP, "cgroup", cgroup_fd, cgroup_ino);
#endif
  errs += _setns(CLONE_NEWIPC, "ipc", ipc_fd, ipc_ino);
  errs += _setns(CLONE_NEWUTS, "uts", uts_fd, uts_ino);
  errs += _setns(CLONE_NEWNET, "net", net_fd, net_ino);
  errs += _setns(CLONE_NEWPID, "pid", pid_fd, pid_ino);
  errs += _setns(CLONE_NEWNS, "mnt", mnt_fd, mnt_ino);

  if (user_err != 0) {
    errs += _setns(CLONE_NEWUSER, "user", user_fd, user_ino);
  }

  return errs == 0 ? 0 : -1;
}

int _chroot () {
  ino_t cur_ino = 0;

  if (init != 1) {
    slurm_error("%s: Not initialized", PLUGIN_NAME);
    return -1;
  }

  if (_get_ino("/", &cur_ino) != 0) {
    slurm_error("%s: Unable to get inode for /: %s", PLUGIN_NAME, strerror(errno));
    return -1;
  }

  if (cur_ino == root_ino) {
    slurm_debug("%s: Already using same root (%lu)", PLUGIN_NAME, root_ino);
    return 0;
  }

  if (fchdir(root_fd) != 0) {
    slurm_error("%s: Unable to change directories to new root dir: %s", PLUGIN_NAME, strerror(errno));
    return -1;
  } else {
    slurm_debug("%s: Changed directory to new root directory", PLUGIN_NAME);
  }

  if (chroot(".") != 0) {
    slurm_error("%s: Unable change root: %s", PLUGIN_NAME, strerror(errno));
    return -1;
  } else {
    slurm_debug("%s: Successfully changed root", PLUGIN_NAME);
  }

  return 0;
}

int _chdir () {
  if (chdir(cwd_path) != 0) {
    slurm_error("%s: Unable to change working directory to %s: %s", PLUGIN_NAME, cwd_path, strerror(errno));
    return -1;
  } else {
    slurm_debug("%s: Changed working directory to %s", PLUGIN_NAME, cwd_path);
  }

  return 0;
}

int _close_all () {
  slurm_debug("%s: Closing all fds", PLUGIN_NAME);
  if (root_fd > 0) {
    close(root_fd);
    root_fd = 0;
  }

#ifdef CLONE_NEWCGROUP
  if (cgroup_fd > 0) {
    close(cgroup_fd);
    cgroup_fd = 0;
  }
#endif

  if (ipc_fd > 0) {
    close(ipc_fd);
    ipc_fd = 0;
  }

  if (uts_fd > 0) {
    close(uts_fd);
    uts_fd = 0;
  }

  if (net_fd > 0) {
    close(net_fd);
    net_fd = 0;
  }

  if (pid_fd > 0) {
    close(pid_fd);
    pid_fd = 0;
  }

  if (mnt_fd > 0) {
    close(mnt_fd);
    mnt_fd = 0;
  }

  if (user_fd > 0) {
    close(user_fd);
    user_fd = 0;
  }

  init = 0;

  return 0;
}

int slurm_spank_task_init_privileged(spank_t sp, int ac, char *argv[]) {
  uint32_t job_id;

  if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) {
    slurm_error("%s: Unable to get job id", PLUGIN_NAME);
    return -1;
  }

  slurm_debug("%s: slurm_spank_task_init_privileged (enter)", PLUGIN_NAME);

  if (spank_remote(sp)) {
    if (_open_job(job_id) != 0) {
      _close_all();
      return -1;
    }
  }

  slurm_debug("%s: slurm_spank_task_init_privileged (leave)", PLUGIN_NAME);

  return 0;
}

int slurm_spank_task_init(spank_t sp, int ac, char *argv[]) {
  slurm_debug("%s: slurm_spank_task_init (enter)", PLUGIN_NAME);

  if (spank_remote(sp)) {
    if (_nsenter() != 0) {
      _close_all();
      return -1;
    }

    if (_chroot() != 0) {
      _close_all();
      return -1;
    }

    _close_all();

    if (_chdir() != 0)
      return -1;

    _continue_as_child();
  }

  slurm_debug("%s: slurm_spank_task_init (leave)", PLUGIN_NAME);

  return 0;
}
