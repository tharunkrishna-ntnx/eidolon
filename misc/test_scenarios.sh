#!/usr/bin/env bash
set -euo pipefail
#
# Apply / verify / reset each misc scenario.  Confirms leftover
# workloads are gone and cluster processes (if any) were not touched.
#
# Usage:
#   misc/test_scenarios.sh
#

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CTL="$ROOT/cli/misc-ctl"
MISC="$ROOT/misc"

echo "=== Building misc workloads ==="
"$MISC/build.sh"

snapshot_cluster() {
  for name in atlas hermes mnemosyne themis styx prometheus oracle; do
    pids=$(pgrep -x "$name" 2>/dev/null | sort | tr '\n' ' ' || true)
    printf '%s:%s\n' "$name" "${pids:-}"
  done
}

BEFORE=$(mktemp)
snapshot_cluster > "$BEFORE"

SCENARIOS=(
  M001_high_cpu
  M002_high_memory
  M003_memory_leak
  M004_netstat
  M005_pidstat
  M006_vmstat
  M007_thread_storm
  M008_io_bound
)

COMMS=(
  task-worker
  cache-daemon
  index-builder
  health-probe
  log-shipper
  page-scanner
  thread-pool
)

fail=0

"$CTL" scenarios list

for id in "${SCENARIOS[@]}"; do
  echo ""
  echo "======== $id ========"
  if ! "$CTL" scenarios apply "$id"; then
    echo "APPLY FAILED: $id"
    fail=1
    "$CTL" scenarios reset || true
    continue
  fi
  sleep 1
  if ! "$CTL" scenarios verify "$id"; then
    echo "VERIFY FAILED: $id"
    fail=1
  fi
  "$CTL" scenarios reset
  sleep 0.5

  leftover=""
  for c in "${COMMS[@]}"; do
    if pgrep -x "$c" >/dev/null 2>&1; then
      leftover+=" $c"
    fi
  done
  if [ -n "$leftover" ]; then
    echo "LEFTOVER PROCESSES after reset:$leftover"
    fail=1
  fi

  # state dir should be gone
  if sudo -n test -e /home/ember/tmp/misc 2>/dev/null || \
     [ -e /home/ember/tmp/misc ]; then
    # Accessible without sudo only if perms allow; try as we can.
    if [ -e /home/ember/tmp/misc ]; then
      echo "LEFTOVER runtime dir: /home/ember/tmp/misc"
      fail=1
    fi
  fi
done

echo ""
echo "=== Cluster process snapshot (before vs after) ==="
AFTER=$(mktemp)
snapshot_cluster > "$AFTER"
if ! diff -q "$BEFORE" "$AFTER" >/dev/null; then
  echo "CLUSTER PIDS CHANGED:"
  diff -u "$BEFORE" "$AFTER" || true
  fail=1
else
  echo "Cluster PIDs unchanged."
  cat "$AFTER"
fi
rm -f "$BEFORE" "$AFTER"

"$CTL" status

echo ""
if [ "$fail" -eq 0 ]; then
  echo "=== ALL MISC SCENARIO TESTS PASSED ==="
else
  echo "=== SOME MISC SCENARIO TESTS FAILED ==="
fi
exit "$fail"
