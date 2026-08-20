#!/usr/bin/env bash
set -euo pipefail
#
# Install local tooling needed for misc (top/mpstat/pidstat/vmstat/netstat) labs.
# Local machine only — does not touch other cluster nodes.
#
# Usage:
#   misc/setup.sh
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=../tools/lib.sh
source "$REPO_ROOT/tools/lib.sh"
ensure_sudo_pass

CAND="${EIDOLON_CANDIDATE_USER:-ember}"
CAND_HOME="/home/$CAND"

echo "=== Eidolon misc training setup (local) ==="

echo "[local] Installing sysstat, net-tools, procps..."
echo "$SUDO_PASS" | sudo -S env DEBIAN_FRONTEND=noninteractive apt-get update -qq
echo "$SUDO_PASS" | sudo -S env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq sysstat net-tools procps

echo "[local] Verifying tools..."
mpstat -V >/dev/null 2>&1 && echo "  mpstat: ok" || echo "  mpstat: MISSING"
pidstat -V >/dev/null 2>&1 && echo "  pidstat: ok" || echo "  pidstat: MISSING"
vmstat -V >/dev/null 2>&1 && echo "  vmstat: ok" || echo "  vmstat: MISSING"
command -v iostat >/dev/null && echo "  iostat: ok" || echo "  iostat: MISSING"
command -v netstat >/dev/null && echo "  netstat: ok" || echo "  netstat: MISSING"
command -v ss >/dev/null && echo "  ss: ok" || echo "  ss: MISSING"
command -v top >/dev/null && echo "  top: ok" || echo "  top: MISSING"

CHEAT="$REPO_ROOT/packaging/misc-tools-cheatsheet.txt"
if [ -f "$CHEAT" ]; then
  echo "[local] Installing cheatsheet for $CAND..."
  echo "$SUDO_PASS" | sudo -S bash -c "
    mkdir -p $CAND_HOME/reference &&
    cp '$CHEAT' $CAND_HOME/reference/misc-tools-cheatsheet.txt &&
    chown -R $CAND:$CAND $CAND_HOME/reference
  "
fi

echo ""
echo "=== Misc setup complete ==="
echo "Build workloads with:  $SCRIPT_DIR/build.sh"
echo "Apply a scenario with: ./cli/misc-ctl scenarios apply M001_high_cpu"
