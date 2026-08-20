#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/lib.sh"

CAND="${EIDOLON_CANDIDATE_USER:-ember}"
CAND_HOME="/home/$CAND"
STYX_PORT="${EIDOLON_STYX_PORT:-7101}"
DISC_PORT="${EIDOLON_DISCOVERY_PORT:-7100}"

ensure_sudo_pass

run_as_cand_local() {
  echo "$SUDO_PASS" | sudo -S -u "$CAND" bash -c "$1" 2>/dev/null
}

run_as_cand_remote() {
  local ip="$1"
  ssh "scorch@$ip" "echo '$SUDO_PASS' | sudo -S -u $CAND bash -c '$2'" 2>/dev/null
}

start_on_node() {
  local ip="$1"
  local is_local="$2"

  run() {
    if [ "$is_local" = "true" ]; then
      run_as_cand_local "$1"
    else
      run_as_cand_remote "$ip" "$1"
    fi
  }

  echo "[$ip] Creating data dirs..."
  run "mkdir -p $CAND_HOME/data/{logs,styx,mnemosyne,hermes/blocks,atlas} $CAND_HOME/tmp"

  echo "[$ip] Starting Styx..."
  run "nohup $CAND_HOME/bin/styx \
    --port=$STYX_PORT --discovery_port=$DISC_PORT \
    --config_dir=$CAND_HOME/config --log_dir=$CAND_HOME/data/logs \
    --tmp_dir=$CAND_HOME/tmp --data_dir=$CAND_HOME/data/styx \
    >> $CAND_HOME/data/logs/styx_stdout.log 2>&1 &"

  sleep 1

  echo "[$ip] Starting Prometheus..."
  run "nohup $CAND_HOME/bin/prometheus \
    --config_dir=$CAND_HOME/config --log_dir=$CAND_HOME/data/logs \
    --tmp_dir=$CAND_HOME/tmp --data_dir=$CAND_HOME/data \
    --candidate_home=$CAND_HOME \
    >> $CAND_HOME/data/logs/prometheus_stdout.log 2>&1 &"

  sleep 2
  echo "[$ip] Services launched."
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

echo "=== Starting Eidolon cluster ==="
echo "Nodes: ${NODES[*]}"
echo ""

for ip in "${NODES[@]}"; do
  if [ "$ip" = "$LOCAL_IP" ]; then
    start_on_node "$ip" "true"
  else
    start_on_node "$ip" "false"
  fi
  echo ""
done

sleep 3
echo "=== Checking discovery... ==="
python3 -c "
import socket
s = socket.create_connection(('127.0.0.1', $STYX_PORT), timeout=5)
s.sendall(b'MEMBERS\n')
s.shutdown(socket.SHUT_WR)
data = b''
while True:
    chunk = s.recv(4096)
    if not chunk: break
    data += chunk
s.close()
print(data.decode())
" 2>&1 || echo "(Could not query Styx)"

echo "Use 'cluster status' (as $CAND) to check service health."
