# Eidolon candidate environment setup -- sourced from .bashrc
# This file is deployed automatically; do not edit on the candidate node.

export PATH="$HOME/bin:$PATH"

# Prompt mimicking a CVM login.
PS1='\u@\h:\w\$ '

# Show cluster info on login.
if [ -f "$HOME/.eidolon_version" ]; then
  _eidolon_ver=$(cat "$HOME/.eidolon_version" 2>/dev/null)
fi
_eidolon_ip=$(hostname -I 2>/dev/null | awk '{print $1}')

cat <<MOTD

  ╔══════════════════════════════════════════════╗
  ║              Eidolon Cluster Node            ║
  ╠══════════════════════════════════════════════╣
  ║  Node IP  : ${_eidolon_ip:-unknown}
  ║  Version  : ${_eidolon_ver:-unknown}
  ║  Hostname : $(hostname)
  ╚══════════════════════════════════════════════╝

  Run 'cluster status' to see the cluster state.

MOTD

unset _eidolon_ver _eidolon_ip

alias ll='ls -alF'
alias logs='ls -lt ~/data/logs/'

# Bash completion for cluster CLI.
if [ -f "$HOME/.bash_completion_cluster" ]; then
  source "$HOME/.bash_completion_cluster"
fi
