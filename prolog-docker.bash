#!/bin/bash

DOCKER_IMAGE=theasp/slurm-dev
NSENTER_DIR=/var/run/slurm-llnl/task_nsenter

exec 1>>/var/log/slurm-llnl/prolog.log 2>&1
set -eo pipefail

function main {
  if [[ -z $SLURM_JOB_ID ]]; then
    error "Unable to determine Slurm job id"
  fi

  DOCKER_NAME="slurm_job${SLURM_JOB_ID}_$(hostname -s)"
  CMD="ln -sf /proc/1 '${NSENTER_DIR}/${SLURM_JOB_ID}' && sleep infinity"

  if ! IFS=: read _ _ USER_UID USER_GID USER_GECOS USER_HOME USER_SHELL < <(getent passwd "${SLURM_JOB_USER}"); then
    error "Unable to look up user ${SLURM_JOB_USER}"
  fi

  LOCAL_IP=$(getent hosts $(hostname -f) | head -n 1 | awk '{ print $1 }')
  if [[ -z $LOCAL_IP ]]; then
    error "Unable to determine local IP address"
  fi

  SLURM_CGROUP=$(cat /proc/self/cgroup | grep ':pids:' | cut -f 3 -d :)
  if [[ -z $SLURM_CGROUP ]]; then
    error "Unable to determine Slurm cgroup"
  fi

  USER_HOME=$(readlink -f "${USER_HOME}")
  
  LOCAL_ALIAS="$(hostname):$LOCAL_IP"
  HOME_VOLUME="${USER_HOME}:${USER_HOME}"

  info "Starting Docker container ${DOCKER_NAME}"
  (set -x;
   docker container run \
          --rm \
          --init \
          --detach=true \
          --name="${DOCKER_NAME}" \
          --tmpfs=/tmp:rw,size=500M,mode=1777 \
          --cgroup-parent="${SLURM_CGROUP}" \
          --uts=host \
          --network=host \
          --userns=host \
          --add-host="${LOCAL_ALIAS}" \
          --env-file=<(env) \
          --security-opt apparmor=unconfined \
          --volume=/etc/slurm-llnl:/etc/slurm-llnl:ro \
          --volume=/etc/munge:/etc/munge:ro \
          --volume=/var/spool/slurm-llnl:/var/spool/slurm-llnl:ro \
          --volume=/var/run/slurm-llnl:/var/run/slurm-llnl \
          --volume=/var/lib/slurm-llnl:/var/lib/slurm-llnl:ro \
          --volume=/var/run/munge:/var/run/munge \
          --volume=/sys/fs/cgroup:/sys/fs/cgroup \
          --volume="${HOME_VOLUME}" \
          --volume="${NSENTER_DIR}" \
          "${DOCKER_IMAGE}" \
          bash -xc "$CMD")

  DOCKER_PID=$(docker inspect "${DOCKER_NAME}" | jq '.[0].State.Pid')

  if [[ -z $DOCKER_PID ]]; then
    error "Unable to find PID for container $CONTAINER"
  fi

  info "Container PID is $DOCKER_PID"
  mkdir -p "${NSENTER_DIR}"
  ln -sf "/proc/${DOCKER_PID}" "${NSENTER_DIR}/${SLURM_JOB_ID}"
}

function log {
  echo "$(date +'%F %T') prolog-docker/${SLURM_JOB_ID} $*"
}

function error {
  log "ERROR: $*" 1>&2
  exit 1
}

function warn {
  log "WARN: $*" 1>&2
}

function info {
  log "INFO: $*" 1>&2
}

main "$@"
