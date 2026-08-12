#!/usr/bin/env bash
# Shared helpers for Eidolon tooling scripts.

SCRIPT_DIR="${SCRIPT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
REPO_ROOT="${REPO_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"

# Parse eidolon.conf and export key values.
parse_eidolon_conf() {
  local conf="$REPO_ROOT/eidolon.conf"
  if [ ! -f "$conf" ]; then
    echo "ERROR: $conf not found" >&2
    return 1
  fi

  local section=""
  while IFS= read -r line || [ -n "$line" ]; do
    line="${line%%#*}"           # strip comments
    line="${line// /}"           # strip spaces
    [[ -z "$line" ]] && continue

    if [[ "$line" =~ ^\[(.+)\]$ ]]; then
      section="${BASH_REMATCH[1]}"
      continue
    fi

    if [[ "$line" =~ ^([^=]+)=(.+)$ ]]; then
      local key="${BASH_REMATCH[1]}"
      local val="${BASH_REMATCH[2]}"
      case "${section}_${key}" in
        users_master)     export EIDOLON_MASTER_USER="$val" ;;
        users_candidate)  export EIDOLON_CANDIDATE_USER="$val" ;;
        network_discovery_port)  export EIDOLON_DISCOVERY_PORT="$val" ;;
        network_styx_port)       export EIDOLON_STYX_PORT="$val" ;;
        network_prometheus_port) export EIDOLON_PROMETHEUS_PORT="$val" ;;
        network_mnemosyne_port)  export EIDOLON_MNEMOSYNE_PORT="$val" ;;
        network_hermes_port)     export EIDOLON_HERMES_PORT="$val" ;;
        network_oracle_port)     export EIDOLON_ORACLE_PORT="$val" ;;
        auth_sudo_pass)          export SUDO_PASS="$val" ;;
        build_static_link)       export EIDOLON_STATIC_LINK="$val" ;;
      esac
    fi
  done < "$conf"
}

# Always parse on source.
parse_eidolon_conf

# Provide a helper that prompts once if SUDO_PASS wasn't set in eidolon.conf.
ensure_sudo_pass() {
  if [ -z "${SUDO_PASS:-}" ]; then
    read -rsp "[eidolon] sudo password: " SUDO_PASS
    echo
    export SUDO_PASS
  fi
}

# Candidate home directory.
candidate_home() {
  eval echo "~${EIDOLON_CANDIDATE_USER}"
}

# Get the Eidolon version from the build output.
eidolon_version() {
  local vfile="$REPO_ROOT/build/deploy/.eidolon_version"
  if [ -f "$vfile" ]; then
    cat "$vfile"
  else
    echo "unknown"
  fi
}

# Run a command on a remote node as a given user.
# Usage: remote_exec <ip> <user> <command...>
remote_exec() {
  local ip="$1" user="$2"
  shift 2
  ssh -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
    "${user}@${ip}" "$@"
}

# Run a command on a remote node as the master user.
remote_master() {
  local ip="$1"; shift
  remote_exec "$ip" "$EIDOLON_MASTER_USER" "$@"
}

# Run a command on a remote node as the candidate user.
remote_candidate() {
  local ip="$1"; shift
  remote_exec "$ip" "$EIDOLON_CANDIDATE_USER" "$@"
}
