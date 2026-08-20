#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib.sh"
ensure_sudo_pass

CAND="${EIDOLON_CANDIDATE_USER:-ember}"
CAND_HOME="/home/$CAND"

STOP_SCRIPT='
# Graceful: kill by PID files
if [ -f CAND_HOME/tmp/prometheus.pid ]; then
  kill $(cat CAND_HOME/tmp/prometheus.pid) 2>/dev/null || true
fi
sleep 2
for f in CAND_HOME/tmp/*.pid; do
  [ -f "$f" ] && kill $(cat "$f") 2>/dev/null || true
done
sleep 1
# Forceful: kill any remaining service processes by name
for proc in styx prometheus hermes mnemosyne themis atlas; do
  pids=$(pgrep -u CAND_USER -x "$proc" 2>/dev/null || true)
  [ -n "$pids" ] && kill $pids 2>/dev/null || true
done
# Also match python3 launchers for styx/prometheus
for proc in styx prometheus; do
  pids=$(pgrep -u CAND_USER -f "bin/$proc" 2>/dev/null || true)
  [ -n "$pids" ] && kill $pids 2>/dev/null || true
done
sleep 1
for proc in styx prometheus hermes mnemosyne themis atlas; do
  pids=$(pgrep -u CAND_USER -x "$proc" 2>/dev/null || true)
  [ -n "$pids" ] && kill -9 $pids 2>/dev/null || true
done
for proc in styx prometheus; do
  pids=$(pgrep -u CAND_USER -f "bin/$proc" 2>/dev/null || true)
  [ -n "$pids" ] && kill -9 $pids 2>/dev/null || true
done
rm -f CAND_HOME/tmp/*.pid CAND_HOME/tmp/*.sock
'

stop_on_node() {
  local ip="$1"
  local is_local="$2"
  local cmd="${STOP_SCRIPT//CAND_HOME/$CAND_HOME}"
  cmd="${cmd//CAND_USER/$CAND}"

  echo "[$ip] Stopping services..."
  if [ "$is_local" = "true" ]; then
    echo "$SUDO_PASS" | sudo -S -u "$CAND" bash -c "$cmd" 2>/dev/null || true
  else
    ssh "scorch@$ip" "echo '$SUDO_PASS' | sudo -S -u $CAND bash -c '$cmd'" 2>/dev/null || true
  fi
  echo "[$ip] Stopped."
}

LOCAL_IP=$(python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(('8.8.8.8', 80))
print(s.getsockname()[0])
s.close()
")

NODES=("$LOCAL_IP")
for arg in "$@"; do
  if [ "$arg" != "$LOCAL_IP" ]; then
    NODES+=("$arg")
  fi
done

echo "=== Stopping Eidolon cluster ==="
echo "Nodes: ${NODES[*]}"
echo ""

for ip in "${NODES[@]}"; do
  if [ "$ip" = "$LOCAL_IP" ]; then
    stop_on_node "$ip" "true"
  else
    stop_on_node "$ip" "false"
  fi
done

echo ""
echo "=== Cluster stopped ==="
