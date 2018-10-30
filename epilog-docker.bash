#!/bin/bash

exec 1>>/var/log/slurm-llnl/epilog.log 2>&1
set -eo pipefail

function log {
  echo "$(date +'%F %T') epilog-docker/${SLURM_JOB_ID:-unknown} $*"
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

if [[ -z $SLURM_JOB_ID ]]; then
  error "Unable to determine Slurm job id"
fi

DOCKER_NAME="slurm_job${SLURM_JOB_ID}_$(hostname -s)"
SLURM_TASK_NSENTER="/var/run/slurm-llnl/task_nsenter/${SLURM_JOB_ID}"

if [[ -e $SLURM_TASK_NSENTER ]]; then
  info "Removing ${SLURM_TASK_NSENTER}"
  rm "${SLURM_TASK_NSENTER}"
fi

if docker inspect "${DOCKER_NAME}" >/dev/null 2>&1; then
  info "Stopping Docker container ${DOCKER_NAME}"
  docker stop "${DOCKER_NAME}"
fi
