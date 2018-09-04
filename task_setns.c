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

#define ENV_NS_CGROUP "SLURM_NS_CGROUP"
#define ENV_NS_IPC    "SLURM_NS_IPC"
#define ENV_NS_NET    "SLURM_NS_NET"
#define ENV_NS_MNT    "SLURM_NS_MNT"
#define ENV_NS_PID    "SLURM_NS_PID"
#define ENV_NS_USER   "SLURM_NS_USER"
#define ENV_NS_UTS    "SLURM_NS_UTS"

SPANK_PLUGIN(task_setns, 1);

#define PLUGIN_NAME "task_setns"

int _setns_path(char *path, int nstype) {
  int fd;

  fd = open(path, O_RDONLY);
  if (fd == -1) {
    slurm_error("%s: Error opening namespace path %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  if (setns(fd, 0) == -1) {
    slurm_error("%s: Error setting namespace path %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  return 0;
}

int _setns_dir_entry(char *dir, int nstype, char *nstype_name) {
  char path[PATH_MAX];
  struct stat buffer;
  
  snprintf(path, PATH_MAX, "%s/%s", dir, nstype_name);

  if (stat(path, &buffer) != 0) {
    slurm_debug("%s: Skipping %s: %s", PLUGIN_NAME, path, strerror(errno));
    return 0;
  } else {
    slurm_debug("%s: Using %s", PLUGIN_NAME, path);
    return _setns_path(path, nstype);
  }
}

int _setns_dir(char* dir) {
  int errs = 0;

  slurm_debug("%s: Checking namespace directory %s", PLUGIN_NAME, dir);

  errs += _setns_dir_entry(dir, CLONE_NEWCGROUP, "cgroup");
  errs += _setns_dir_entry(dir, CLONE_NEWIPC, "ipc");
  errs += _setns_dir_entry(dir, CLONE_NEWNS, "mnt");
  errs += _setns_dir_entry(dir, CLONE_NEWPID, "pid");
  errs += _setns_dir_entry(dir, CLONE_NEWUSER, "user");
  errs += _setns_dir_entry(dir, CLONE_NEWUTS, "uts");

  if (errs != 0) {
    slurm_error("%s: Unable to clone all namespaces in %s", PLUGIN_NAME, dir);
    return -1;
  }

  return 0;
}

int _setns(spank_t sp) {
  char dir[PATH_MAX];
  uint32_t job_id;

  if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) {
    slurm_error("%s: Unable to clone all namespaces in %s", PLUGIN_NAME, dir);
    return -1;
  }

  snprintf(dir, PATH_MAX, "%s/%d", "/var/run/slurm-llnl/task_setns", job_id);
  return _setns_dir(dir);
}


int slurm_spank_task_init_privileged (spank_t sp, int ac, char *argv[]) {
  int result = 0;

  slurm_debug("%s: slurm_spank_task_post_fork (enter)", PLUGIN_NAME);

  result=_setns(sp);

  slurm_debug("%s: slurm_spank_task_post_fork (leave)", PLUGIN_NAME);

  return result;
}
