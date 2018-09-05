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
    slurm_error("%s: Unable to use %s from %s: %s", PLUGIN_NAME, nstype_name, path, strerror(errno));
    return -1;
  } else {
    slurm_debug("%s: Using %s from %s", PLUGIN_NAME, nstype_name, path);
    return _setns_path(path, nstype);
  }
}

int _setns_dir(char* dir) {
  int errs = 0;

  slurm_debug("%s: Checking namespace directory %s", PLUGIN_NAME, dir);

  errs += _setns_dir_entry(dir, CLONE_NEWCGROUP, "cgroup");
  errs += _setns_dir_entry(dir, CLONE_NEWIPC, "ipc");
  errs += _setns_dir_entry(dir, CLONE_NEWUTS, "uts");
  errs += _setns_dir_entry(dir, CLONE_NEWNET, "net");
  errs += _setns_dir_entry(dir, CLONE_NEWPID, "pid");
  errs += _setns_dir_entry(dir, CLONE_NEWNS, "mnt");

  if (errs != 0) {
    slurm_error("%s: Unable to clone all namespaces in %s", PLUGIN_NAME, dir);
    return -1;
  }

  return 0;
}

int _setns(spank_t sp, int required) {
  char dir[PATH_MAX];
  uint32_t job_id;

  if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) {
    slurm_error("%s: Unable to get job id", PLUGIN_NAME);
    return -1;
  }

  snprintf(dir, PATH_MAX, "%s/%d", "/var/run/slurm-llnl/task_setns", job_id);

  if (_setns_dir(dir) != 0 && required != 0)
    return -1;

  return 0;
}

int slurm_spank_task_init_privileged (spank_t sp, int ac, char *argv[]) {
  int result = 0;

  slurm_debug("%s: slurm_spank_task_init_privileged (enter)", PLUGIN_NAME);

  if (spank_remote(sp)) {
    result=_setns(sp, 1);
  }

  slurm_debug("%s: slurm_spank_task_init_privileged (leave)", PLUGIN_NAME);

  return result;
}