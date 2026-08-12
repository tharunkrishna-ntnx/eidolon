#!/usr/bin/env bash
set -euo pipefail
#
# First-time bootstrap for an Eidolon node.
# Installs dependencies, builds, deploys, sets up the candidate environment,
# and starts the cluster services.
#
# Usage:
#   bootstrap.sh                          Use defaults from eidolon.conf
#   bootstrap.sh --master-user foo --candidate-user bar

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Allow overriding user names from the command line.
INSTALL_PERF_TOOLS=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --master-user)    sed -i "s/^master = .*/master = $2/" "$REPO_ROOT/eidolon.conf"; shift 2 ;;
    --candidate-user) sed -i "s/^candidate = .*/candidate = $2/" "$REPO_ROOT/eidolon.conf"; shift 2 ;;
    --perf-tools)     INSTALL_PERF_TOOLS=true; shift ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

source "$SCRIPT_DIR/lib.sh"

echo "╔══════════════════════════════════════════════╗"
echo "║         Eidolon Bootstrap                    ║"
echo "╠══════════════════════════════════════════════╣"
echo "║  Master user    : $EIDOLON_MASTER_USER"
echo "║  Candidate user : $EIDOLON_CANDIDATE_USER"
echo "╚══════════════════════════════════════════════╝"
echo ""

# ---- Step 1: System dependencies ----
echo "=== Step 1/6: Installing system dependencies ==="
sudo apt-get update -qq
sudo apt-get install -y -qq \
  build-essential cmake git python3 python3-pip python3-venv rsync netcat-openbsd
if [ "$INSTALL_PERF_TOOLS" = "true" ]; then
  echo "  Including perf/BPF tooling..."
  sudo apt-get install -y -qq \
    linux-perf bpfcc-tools bpftrace linux-headers-"$(uname -r)" 2>/dev/null || \
    echo "  WARNING: some perf/BPF packages may not be available"
  sudo sysctl -w kernel.perf_event_paranoid=1 2>/dev/null || true
  sudo sysctl -w kernel.kptr_restrict=0 2>/dev/null || true
fi
echo ""

# ---- Step 2: Python dependencies ----
echo "=== Step 2/6: Installing Python dependencies ==="
pip3 install --user -q -r "$REPO_ROOT/requirements.txt" || \
  pip3 install --break-system-packages --user -q -r "$REPO_ROOT/requirements.txt" 2>/dev/null || true
echo ""

# ---- Step 3: Build ----
echo "=== Step 3/6: Building ==="
"$SCRIPT_DIR/build.sh"
echo ""

# ---- Step 4: Setup candidate environment ----
echo "=== Step 4/6: Setting up candidate environment ==="
"$SCRIPT_DIR/setup-node.sh"
echo ""

# ---- Step 5: Deploy locally ----
echo "=== Step 5/6: Deploying to local candidate user ==="
"$SCRIPT_DIR/deploy.sh" --local
echo ""

# ---- Step 6: Start services ----
echo "=== Step 6/6: Starting cluster services ==="
CAND_HOME="$(candidate_home)"

start_candidate_service() {
  local svc="$1"
  shift
  local bin="$CAND_HOME/bin/$svc"
  local pidfile="$CAND_HOME/tmp/${svc}.pid"

  if [ -f "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
    echo "  $svc already running (pid=$(cat "$pidfile"))"
    return
  fi

  echo "  Starting $svc ..."
  sudo -u "$EIDOLON_CANDIDATE_USER" bash -c "\
    nohup $bin \
      --config_dir=$CAND_HOME/config \
      --log_dir=$CAND_HOME/data/logs \
      --tmp_dir=$CAND_HOME/tmp \
      --data_dir=$CAND_HOME/data/$svc \
      --blocks_dir=$CAND_HOME/data/hermes/blocks \
      $* \
      >> $CAND_HOME/data/logs/${svc}_stdout.log 2>&1 &"
  sleep 1
  if [ -f "$pidfile" ]; then
    echo "  $svc started (pid=$(cat "$pidfile"))"
  else
    echo "  $svc may not have started (no pidfile yet)"
  fi
}

# Start in dependency order.
start_candidate_service styx
sleep 2
start_candidate_service mnemosyne
start_candidate_service hermes
start_candidate_service themis
start_candidate_service atlas

echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║         Bootstrap Complete!                  ║"
echo "╚══════════════════════════════════════════════╝"
echo ""
echo "The candidate can now SSH in as '$EIDOLON_CANDIDATE_USER' and run 'cluster status'."
echo "Repeat bootstrap on other nodes; they will auto-discover this cluster."
