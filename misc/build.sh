#!/usr/bin/env bash
set -euo pipefail
#
# Build the standalone misc workload binaries.
# Output: misc/build/bin/
#

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$ROOT/build"
cd "$ROOT/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc 2>/dev/null || echo 2)"

echo ""
echo "Built misc workloads:"
ls -l "$ROOT/build/bin"
