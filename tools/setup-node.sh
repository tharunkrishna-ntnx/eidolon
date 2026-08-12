#!/usr/bin/env bash
set -euo pipefail
#
# Per-node setup: creates the candidate user's directory structure, installs
# bashrc additions and MOTD.  Run once per node (or after a clean wipe).
#
# Usage: setup-node.sh [--remote <ip>]
#   Without --remote, sets up the local node.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

TARGET_IP=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --remote) TARGET_IP="$2"; shift 2 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

CAND_USER="$EIDOLON_CANDIDATE_USER"
CAND_HOME="$(candidate_home)"

run_cmd() {
  if [ -n "$TARGET_IP" ]; then
    remote_exec "$TARGET_IP" "$EIDOLON_MASTER_USER" "sudo -u $CAND_USER $*"
  else
    sudo -u "$CAND_USER" bash -c "$*"
  fi
}

echo "Setting up candidate environment for user '$CAND_USER'..."
if [ -n "$TARGET_IP" ]; then
  echo "  Target: $TARGET_IP"
fi

# Create directory structure.
for d in bin config data/logs data/mnemosyne data/hermes/blocks tmp lib; do
  run_cmd "mkdir -p $CAND_HOME/$d"
done
echo "  Directory structure created."

# Install bashrc additions (idempotent).
MARKER="# >>> eidolon >>>"
BASHRC="$CAND_HOME/.bashrc"

inject_bashrc() {
  # We need to write as the candidate user.
  local tmp
  tmp=$(mktemp)
  cat > "$tmp" <<BASHRC_BLOCK
$MARKER
source "\$HOME/.eidolon_bashrc" 2>/dev/null
# <<< eidolon <<<
BASHRC_BLOCK

  if [ -n "$TARGET_IP" ]; then
    scp -o BatchMode=yes "$tmp" "${EIDOLON_MASTER_USER}@${TARGET_IP}:/tmp/_eidolon_bashrc_snippet"
    remote_exec "$TARGET_IP" "$EIDOLON_MASTER_USER" \
      "sudo -u $CAND_USER bash -c 'grep -q \"$MARKER\" $BASHRC 2>/dev/null || cat /tmp/_eidolon_bashrc_snippet >> $BASHRC'"
  else
    sudo -u "$CAND_USER" bash -c "grep -q '$MARKER' $BASHRC 2>/dev/null || cat $tmp >> $BASHRC"
  fi
  rm -f "$tmp"
}

inject_bashrc
echo "  .bashrc updated."

# Copy the bashrc source file.
DEPLOY_DIR="$REPO_ROOT/build/deploy"
if [ -f "$DEPLOY_DIR/bashrc_candidate.sh" ]; then
  if [ -n "$TARGET_IP" ]; then
    scp -o BatchMode=yes "$DEPLOY_DIR/bashrc_candidate.sh" \
      "${EIDOLON_MASTER_USER}@${TARGET_IP}:/tmp/_eidolon_bashrc"
    remote_exec "$TARGET_IP" "$EIDOLON_MASTER_USER" \
      "sudo cp /tmp/_eidolon_bashrc $CAND_HOME/.eidolon_bashrc && sudo chown $CAND_USER:$CAND_USER $CAND_HOME/.eidolon_bashrc"
  else
    sudo cp "$DEPLOY_DIR/bashrc_candidate.sh" "$CAND_HOME/.eidolon_bashrc"
    sudo chown "$CAND_USER:$CAND_USER" "$CAND_HOME/.eidolon_bashrc"
  fi
  echo "  Eidolon bashrc installed."
fi

echo "Node setup complete."
