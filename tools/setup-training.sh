#!/usr/bin/env bash
set -euo pipefail
#
# Install perf/BPF tooling and FlameGraph scripts on an already-bootstrapped
# Eidolon cluster.  Run from the master user on each node, or pass node IPs
# to install remotely.
#
# Usage:
#   setup-training.sh                   Install locally
#   setup-training.sh 10.0.0.2 10.0.0.3  Install on remote nodes too

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
ensure_sudo_pass

CAND="$EIDOLON_CANDIDATE_USER"
CAND_HOME="/home/$CAND"

install_on_node() {
  local ip="$1"
  local is_local="$2"

  echo "[$ip] Installing perf/BPF tooling..."

  local install_cmds='
    apt-get update -qq &&
    apt-get install -y -qq linux-perf bpfcc-tools bpftrace linux-headers-$(uname -r) 2>/dev/null || true
    sysctl -w kernel.perf_event_paranoid=1 2>/dev/null || true
    sysctl -w kernel.kptr_restrict=0 2>/dev/null || true
  '

  if [ "$is_local" = "true" ]; then
    echo "$SUDO_PASS" | sudo -S bash -c "$install_cmds" 2>/dev/null
  else
    ssh "scorch@$ip" "echo '$SUDO_PASS' | sudo -S bash -c '$install_cmds'" 2>/dev/null
  fi

  echo "[$ip] Installing FlameGraph scripts..."
  local fg_cmds="
    cd /tmp &&
    rm -rf FlameGraph &&
    git clone --depth 1 https://github.com/brendangregg/FlameGraph.git 2>/dev/null &&
    cp FlameGraph/stackcollapse-perf.pl $CAND_HOME/bin/ &&
    cp FlameGraph/flamegraph.pl $CAND_HOME/bin/ &&
    chown $CAND:$CAND $CAND_HOME/bin/stackcollapse-perf.pl $CAND_HOME/bin/flamegraph.pl &&
    chmod +x $CAND_HOME/bin/stackcollapse-perf.pl $CAND_HOME/bin/flamegraph.pl &&
    rm -rf FlameGraph
  "

  if [ "$is_local" = "true" ]; then
    echo "$SUDO_PASS" | sudo -S bash -c "$fg_cmds" 2>/dev/null
  else
    ssh "scorch@$ip" "echo '$SUDO_PASS' | sudo -S bash -c '$fg_cmds'" 2>/dev/null
  fi

  # Deploy reference material if available.
  local cheatsheet="$REPO_ROOT/packaging/perf-bpf-cheatsheet.txt"
  if [ -f "$cheatsheet" ]; then
    echo "[$ip] Deploying reference material..."
    if [ "$is_local" = "true" ]; then
      echo "$SUDO_PASS" | sudo -S bash -c "
        mkdir -p $CAND_HOME/reference &&
        cp $cheatsheet $CAND_HOME/reference/ &&
        chown -R $CAND:$CAND $CAND_HOME/reference
      " 2>/dev/null
    else
      scp -o BatchMode=yes "$cheatsheet" "scorch@$ip:/tmp/_cheatsheet.txt" 2>/dev/null
      ssh "scorch@$ip" "echo '$SUDO_PASS' | sudo -S bash -c '
        mkdir -p $CAND_HOME/reference &&
        cp /tmp/_cheatsheet.txt $CAND_HOME/reference/perf-bpf-cheatsheet.txt &&
        chown -R $CAND:$CAND $CAND_HOME/reference &&
        rm -f /tmp/_cheatsheet.txt
      '" 2>/dev/null
    fi
  fi

  echo "[$ip] Done."
}

LOCAL_IP=$(python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(('8.8.8.8', 80))
print(s.getsockname()[0])
s.close()
")

echo "=== Eidolon Training Environment Setup ==="
install_on_node "$LOCAL_IP" "true"

for ip in "$@"; do
  if [ "$ip" != "$LOCAL_IP" ]; then
    install_on_node "$ip" "false"
  fi
done

echo ""
echo "=== Training setup complete ==="
echo "Candidate users now have: perf, bpftrace, bcc-tools, flamegraph.pl"
