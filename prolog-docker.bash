#!/bin/bash

if [[ -z ${SLURM_JOB_ID} ]]; then
  exit 1
fi

touch /this-is-slurm

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


exec 1>>/var/log/slurm-llnl/prolog.log 2>&1
set -o pipefail

CGROUP_PARENT=$(cat /proc/self/cgroup | grep ':pids:' | cut -f 3 -d :)
CONTAINER="slurm_job${SLURM_JOB_ID}_$(hostname -s)"
export SETNS_DIR="/var/run/slurm-llnl/task_setns"

IP=$(getent hosts $(hostname -f) | head -n 1 | awk '{ print $1 }')

if [[ -z $IP ]]; then
  error "Unable to determine local IP address"
fi

LOCAL_ALIAS="$(hostname):$IP"

info "Starting container $CONTAINER"
docker container run \
       --rm \
       --init \
       --detach=true \
       --name=${CONTAINER} \
       --tmpfs=/tmp:rw,size=500M,mode=1777 \
       --cgroup-parent=$CGROUP_PARENT \
       --volume=/srv/slurm/etc:/etc/slurm-llnl:ro \
       --volume=/srv/slurm/etc/munge:/etc/munge:ro \
       --volume=/srv/slurm/var-spool:/var/spool/slurm-llnl:ro \
       --volume=/srv/slurm/var-lib:/var/lib/slurm-llnl:ro \
       --volume=/srv/slurm/var-run-munge:/var/run/munge \
       --volume=/sys/fs/cgroup:/sys/fs/cgroup \
       --volume=/srv/slurm/var-run:/var/run/slurm-llnl \
       --volume="$SETNS_DIR" \
       --uts=host \
       --network=host \
       --userns=host \
       --add-host=$LOCAL_ALIAS \
       --env-file=<(env) \
       theasp/slurm-dev bash -xc 'ln -sf /proc/1 "${SETNS_DIR}/${SLURM_JOB_ID}" && cat /proc/sys/kernel/random/uuid > /tmp/container_uuid && date > /tmp/container_start && sleep infinity'

PID=$(docker inspect $CONTAINER | jq '.[0].State.Pid')

if [[ -z $PID ]]; then
  error "Unable to find PID for container $CONTAINER"
fi

info "Container PID is $PID"
mkdir -p "${SETNS_DIR}"
ln -sf "/proc/${PID}" "${SETNS_DIR}/${SLURM_JOB_ID}"
