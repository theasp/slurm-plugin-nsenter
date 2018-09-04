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

SPANK_PLUGIN(task_namespace, 1);

#define PLUGIN_NAME "task_namespace"

int _clear_env(spank_t sp) {
  spank_err_t err;

  slurm_debug("%s: clear-env", PLUGIN_NAME);

  if ((err = spank_unsetenv(sp, ENV_NS_CGROUP)) != ESPANK_SUCCESS) {
    slurm_error("%s: Unable to unset %s: %s", PLUGIN_NAME, ENV_NS_CGROUP, spank_strerror(err));
    return -1;
  }

  if (spank_unsetenv(sp, ENV_NS_IPC) != ESPANK_SUCCESS)
    return -1;

  if (spank_unsetenv(sp, ENV_NS_NET) != ESPANK_SUCCESS)
    return -1;

  if (spank_unsetenv(sp, ENV_NS_MNT) != ESPANK_SUCCESS)
    return -1;

  if (spank_unsetenv(sp, ENV_NS_PID) != ESPANK_SUCCESS)
    return -1;

  if (spank_unsetenv(sp, ENV_NS_USER) != ESPANK_SUCCESS)
    return -1;

  if (spank_unsetenv(sp, ENV_NS_UTS) != ESPANK_SUCCESS)
    return -1;

  return 0;
}

int _setns_path(char *path, int nstype) {
  int fd;

  fd = open(path, O_RDONLY);
  if (fd == -1) {
    slurm_error("%s: Namespace path %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  if (setns(fd, 0) == -1) {
    slurm_error("%s: Namespace path %s: %s", PLUGIN_NAME, path, strerror(errno));
    return -1;
  }

  return 0;
}

int _setns_env(spank_t sp) {
  char path[PATH_MAX];

  if (spank_getenv (sp, ENV_NS_CGROUP, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using CGROUP namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWCGROUP) != 0) {
      return -1;
    }
  }

  if (spank_getenv (sp, ENV_NS_IPC, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using IPC namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWIPC) != 0) {
      return -1;
    }
  }

  if (spank_getenv (sp, ENV_NS_NET, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using NET namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWNET) != 0) {
      return -1;
    }
  }

  if (spank_getenv (sp, ENV_NS_MNT, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using MNT namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWNS) != 0) {
      return -1;
    }
  }

  if (spank_getenv (sp, ENV_NS_PID, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using PID namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWPID) != 0) {
      return -1;
    }
  }

  if (spank_getenv (sp, ENV_NS_USER, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using USER namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWUSER) != 0) {
      return -1;
    }
  }

  if (spank_getenv (sp, ENV_NS_UTS, path, PATH_MAX) == ESPANK_SUCCESS) {
    slurm_error("%s: Using UTS namespace %s", PLUGIN_NAME, path);
    if (_setns_path(path, CLONE_NEWUTS) != 0) {
      return -1;
    }
  }

  return 0;
}


int slurm_spank_init_post_opt (spank_t sp, int ac, char *argv[]) {
  int result = 0;

  slurm_debug("%s: slurm_spank_init_post_opt (enter)", PLUGIN_NAME);

  if (spank_remote(sp))
    result=_clear_env(sp);

  slurm_debug("%s: slurm_spank_init_post_opt (leave)", PLUGIN_NAME);

  return result;
}

int slurm_spank_task_post_fork (spank_t sp, int ac, char *argv[]) {
  int result = 0;

  slurm_debug("%s: slurm_spank_task_post_fork (enter)", PLUGIN_NAME);

  result=_setns_env(sp);

  slurm_debug("%s: slurm_spank_task_post_fork (leave)", PLUGIN_NAME);

  return result;
}
