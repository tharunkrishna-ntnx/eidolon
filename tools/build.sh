#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=/dev/null
source "$SCRIPT_DIR/lib.sh"

BUILD_DIR="$REPO_ROOT/build"
DEPLOY_DIR="$BUILD_DIR/deploy"

echo "=== Eidolon Build ==="
echo "Repo: $REPO_ROOT"

# ---- Build C++ services ----
echo ""
echo "--- Building C++ services ---"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$REPO_ROOT" -DCMAKE_BUILD_TYPE=Release
cmake --build . -j "$(nproc)"
echo "C++ build complete."

# ---- Assemble deploy directory ----
echo ""
echo "--- Assembling deploy package ---"
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"/{bin,config}

# C++ binaries
for svc in hermes mnemosyne themis atlas; do
  if [ -f "$BUILD_DIR/services/$svc/$svc" ]; then
    cp "$BUILD_DIR/services/$svc/$svc" "$DEPLOY_DIR/bin/"
    echo "  Copied $svc binary"
  fi
done

# Python services -- deploy source to lib/ with launcher shims in bin/
for pydir in styx prometheus; do
  src="$REPO_ROOT/services/$pydir"
  dest="$DEPLOY_DIR/bin/$pydir"
  if [ -d "$src" ]; then
    mkdir -p "$DEPLOY_DIR/lib/$pydir"
    for pyfile in "$src"/*.py; do
      [ -f "$pyfile" ] || continue
      cp "$pyfile" "$DEPLOY_DIR/lib/$pydir/"
      echo "  Packaged $(basename "$pyfile")"
    done
    # Launcher shim that adds lib dir to sys.path and runs main()
    cat > "$dest" <<LAUNCHER
#!/usr/bin/env python3
import sys, os, runpy
lib = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib', '$pydir')
sys.path.insert(0, lib)
sys.argv[0] = '${pydir}'
runpy.run_module('${pydir}', run_name='__main__', alter_sys=True)
LAUNCHER
    chmod +x "$dest"
  fi
done

# Cluster CLI
if [ -f "$REPO_ROOT/cli/cluster" ]; then
  cp "$REPO_ROOT/cli/cluster" "$DEPLOY_DIR/bin/cluster"
  chmod +x "$DEPLOY_DIR/bin/cluster"
  echo "  Copied cluster CLI"
fi

# Default configs
cp "$REPO_ROOT/config/"*.gflags "$DEPLOY_DIR/config/" 2>/dev/null || true
echo "  Copied config templates"

# Packaging files
cp "$REPO_ROOT/packaging/bashrc_candidate.sh" "$DEPLOY_DIR/"
cp "$REPO_ROOT/packaging/motd.txt" "$DEPLOY_DIR/"
cp "$REPO_ROOT/packaging/bash_completion_cluster.sh" "$DEPLOY_DIR/"
cp "$REPO_ROOT/packaging/bash_completion_eidolon_ctl.sh" "$DEPLOY_DIR/" 2>/dev/null || true

# Reference material (cheatsheets, etc.)
if ls "$REPO_ROOT/packaging/"*.txt 1>/dev/null 2>&1; then
  mkdir -p "$DEPLOY_DIR/reference"
  cp "$REPO_ROOT/packaging/"*.txt "$DEPLOY_DIR/reference/" 2>/dev/null || true
  echo "  Copied reference material"
fi

# Oracle (master-side only, not deployed to candidate)
mkdir -p "$DEPLOY_DIR/oracle"
cp "$REPO_ROOT/services/oracle/oracle.py" "$DEPLOY_DIR/oracle/"
cp "$REPO_ROOT/services/oracle/workload_profiles.py" "$DEPLOY_DIR/oracle/"
cp "$REPO_ROOT/services/oracle/oracle.yaml" "$DEPLOY_DIR/oracle/"

# Version stamp
EIDOLON_VERSION=$(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo "dev")
echo "$EIDOLON_VERSION" > "$DEPLOY_DIR/.eidolon_version"
echo "  Version: $EIDOLON_VERSION"

echo ""
echo "=== Build complete ==="
echo "Deploy package: $DEPLOY_DIR"
