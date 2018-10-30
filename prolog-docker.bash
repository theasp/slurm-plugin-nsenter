#!/bin/bash

exec 1>>/var/log/slurm-llnl/prolog.log 2>&1
set -eo pipefail

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

if [[ -z $SLURM_JOB_ID ]]; then
  error "Unable to determine Slurm job id"
fi


DOCKER_IMAGE=theasp/slurm-dev
DOCKER_NAME="slurm_job${SLURM_JOB_ID}_$(hostname -s)"
SLURM_CGROUP=$(cat /proc/self/cgroup | grep ':pids:' | cut -f 3 -d :)
NSENTER_DIR="/var/run/slurm-llnl/task_nsenter"
LOCAL_IP=$(getent hosts $(hostname -f) | head -n 1 | awk '{ print $1 }')

if [[ -z $LOCAL_IP ]]; then
  error "Unable to determine local IP address"
fi

if [[ -z $SLURM_CGROUP ]]; then
  error "Unable to determine Slurm cgroup"
fi

LOCAL_ALIAS="$(hostname):$LOCAL_IP"

info "Starting Docker container ${DOCKER_NAME}"
docker container run \
       --rm \
       --init \
       --detach=true \
       --name="${DOCKER_NAME}" \
       --tmpfs=/tmp:rw,size=500M,mode=1777 \
       --cgroup-parent="${SLURM_CGROUP}" \
       --volume=/srv/slurm/etc:/etc/slurm-llnl:ro \
       --volume=/srv/slurm/etc/munge:/etc/munge:ro \
       --volume=/srv/slurm/var-spool:/var/spool/slurm-llnl:ro \
       --volume=/srv/slurm/var-lib:/var/lib/slurm-llnl:ro \
       --volume=/srv/slurm/var-run-munge:/var/run/munge \
       --volume=/sys/fs/cgroup:/sys/fs/cgroup \
       --volume=/srv/slurm/var-run:/var/run/slurm-llnl \
       --volume="${NSENTER_DIR}" \
       --uts=host \
       --network=host \
       --userns=host \
       --add-host="${LOCAL_ALIAS}" \
       --env-file=<(env) \
       "${DOCKER_IMAGE}" \
       bash -xc 'ln -sf /proc/1 "${NSENTER_DIR}/${SLURM_JOB_ID}" && cat /proc/sys/kernel/random/uuid > /tmp/container_uuid && date > /tmp/container_start && sleep infinity'

DOCKER_PID=$(docker inspect "${DOCKER_NAME}" | jq '.[0].State.Pid')

if [[ -z $DOCKER_PID ]]; then
  error "Unable to find PID for container $CONTAINER"
fi

info "Container PID is $DOCKER_PID"
mkdir -p "${NSENTER_DIR}"
ln -sf "/proc/${DOCKER_PID}" "${NSENTER_DIR}/${SLURM_JOB_ID}"
