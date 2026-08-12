#!/usr/bin/env bash
set -euo pipefail
#
# Deploy Eidolon artifacts to the candidate user on one or more nodes.
#
# Usage:
#   deploy.sh                     Deploy to all discovered cluster nodes
#   deploy.sh --local             Deploy to this node only
#   deploy.sh --nodes 1.2.3.4,5.6.7.8   Deploy to specific nodes

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

DEPLOY_DIR="$REPO_ROOT/build/deploy"
if [ ! -d "$DEPLOY_DIR" ]; then
  echo "ERROR: Deploy directory not found.  Run build.sh first." >&2
  exit 1
fi

MODE="all"
NODE_LIST=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --local)  MODE="local"; shift ;;
    --nodes)  MODE="list"; NODE_LIST="$2"; shift 2 ;;
    *)        echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

CAND_USER="$EIDOLON_CANDIDATE_USER"
CAND_HOME="$(candidate_home)"
LOCAL_IP="$(hostname -I | awk '{print $1}')"

install_sudoers() {
  local ip="$1"
  local sudoers_content
  sudoers_content="$(cat <<'SUDOEOF'
# Eidolon: allow candidate user to run perf/BPF tracing tools only
CANDIDATE_USER ALL=(root) NOPASSWD: /usr/bin/perf *, /usr/bin/perf, \
    /usr/bin/strace *, /usr/bin/strace, \
    /usr/bin/bpftrace *, /usr/bin/bpftrace, \
    /usr/sbin/biolatency-bpfcc *, /usr/sbin/biolatency-bpfcc, \
    /usr/sbin/offcputime-bpfcc *, /usr/sbin/offcputime-bpfcc, \
    /usr/sbin/tcpconnect-bpfcc *, /usr/sbin/tcpconnect-bpfcc, \
    /usr/sbin/opensnoop-bpfcc *, /usr/sbin/opensnoop-bpfcc
SUDOEOF
)"
  sudoers_content="${sudoers_content//CANDIDATE_USER/$CAND_USER}"
  local b64
  b64="$(echo "$sudoers_content" | base64 -w0)"
  local cmd="echo $b64 | base64 -d > /etc/sudoers.d/eidolon-training && chmod 0440 /etc/sudoers.d/eidolon-training"

  if [ "$ip" = "$LOCAL_IP" ] || [ "$ip" = "127.0.0.1" ]; then
    sudo bash -c "$cmd" 2>/dev/null
  else
    remote_master "$ip" "sudo bash -c '$cmd'" 2>/dev/null
  fi
}

deploy_to_node() {
  local ip="$1"
  echo "Deploying to $ip ..."

  install_sudoers "$ip"

  if [ "$ip" = "$LOCAL_IP" ] || [ "$ip" = "127.0.0.1" ]; then
    # Local deployment via sudo.
    sudo rsync -a --delete "$DEPLOY_DIR/bin/" "$CAND_HOME/bin/"
    sudo rsync -a "$DEPLOY_DIR/config/" "$CAND_HOME/config/"
    sudo rsync -a "$DEPLOY_DIR/lib/" "$CAND_HOME/lib/" 2>/dev/null || true
    sudo cp "$DEPLOY_DIR/.eidolon_version" "$CAND_HOME/.eidolon_version"
    sudo cp "$DEPLOY_DIR/bashrc_candidate.sh" "$CAND_HOME/.eidolon_bashrc"
    sudo cp "$DEPLOY_DIR/bash_completion_cluster.sh" "$CAND_HOME/.bash_completion_cluster" 2>/dev/null || true
    if [ -d "$DEPLOY_DIR/reference" ]; then
      sudo mkdir -p "$CAND_HOME/reference"
      sudo rsync -a "$DEPLOY_DIR/reference/" "$CAND_HOME/reference/"
    fi
    sudo chown -R "$CAND_USER:$CAND_USER" "$CAND_HOME/bin" "$CAND_HOME/config" \
      "$CAND_HOME/lib" "$CAND_HOME/.eidolon_version" "$CAND_HOME/.eidolon_bashrc" \
      "$CAND_HOME/.bash_completion_cluster" 2>/dev/null
    sudo chown -R "$CAND_USER:$CAND_USER" "$CAND_HOME/reference" 2>/dev/null || true
    echo "  Local deploy complete."
  else
    # Remote deployment: rsync to master user's temp, then sudo-copy.
    local tmp_dir="/tmp/_eidolon_deploy_$$"
    remote_master "$ip" "mkdir -p $tmp_dir"
    rsync -az --delete -e "ssh -o BatchMode=yes -o StrictHostKeyChecking=no" \
      "$DEPLOY_DIR/" "${EIDOLON_MASTER_USER}@${ip}:${tmp_dir}/"

    remote_master "$ip" "\
      sudo rsync -a --delete ${tmp_dir}/bin/ $CAND_HOME/bin/ && \
      sudo rsync -a ${tmp_dir}/config/ $CAND_HOME/config/ && \
      sudo rsync -a ${tmp_dir}/lib/ $CAND_HOME/lib/ 2>/dev/null; \
      sudo cp ${tmp_dir}/.eidolon_version $CAND_HOME/.eidolon_version && \
      sudo cp ${tmp_dir}/bashrc_candidate.sh $CAND_HOME/.eidolon_bashrc && \
      sudo cp ${tmp_dir}/bash_completion_cluster.sh $CAND_HOME/.bash_completion_cluster 2>/dev/null; \
      if [ -d ${tmp_dir}/reference ]; then \
        sudo mkdir -p $CAND_HOME/reference && \
        sudo rsync -a ${tmp_dir}/reference/ $CAND_HOME/reference/; \
      fi; \
      sudo chown -R $CAND_USER:$CAND_USER $CAND_HOME/bin $CAND_HOME/config \
        $CAND_HOME/lib $CAND_HOME/.eidolon_version $CAND_HOME/.eidolon_bashrc \
        $CAND_HOME/.bash_completion_cluster 2>/dev/null && \
      sudo chown -R $CAND_USER:$CAND_USER $CAND_HOME/reference 2>/dev/null; \
      rm -rf ${tmp_dir}"
    echo "  Remote deploy to $ip complete."
  fi
}

discover_nodes() {
  # Try to ask local Styx for the membership list.
  local styx_port="${EIDOLON_STYX_PORT:-7101}"
  local nodes
  nodes=$(echo "MEMBERS" | timeout 3 nc -q1 127.0.0.1 "$styx_port" 2>/dev/null | \
    awk '/^[0-9]/ {print $1}') || true
  if [ -n "$nodes" ]; then
    echo "$nodes"
  else
    # Fallback: just the local node.
    echo "$LOCAL_IP"
  fi
}

ensure_sudo_pass
echo "$SUDO_PASS" | sudo -S -v 2>/dev/null

echo "=== Eidolon Deploy ==="
echo "Version: $(eidolon_version)"

case "$MODE" in
  local)
    deploy_to_node "$LOCAL_IP"
    ;;
  list)
    IFS=',' read -ra NODES <<< "$NODE_LIST"
    for ip in "${NODES[@]}"; do
      deploy_to_node "$ip"
    done
    ;;
  all)
    echo "Discovering cluster nodes..."
    while IFS= read -r ip; do
      [ -n "$ip" ] && deploy_to_node "$ip"
    done <<< "$(discover_nodes)"
    ;;
esac

echo ""
echo "=== Deploy complete ==="
