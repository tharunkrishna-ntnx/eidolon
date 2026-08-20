# Bash completion for the 'cluster' CLI tool.
# Source this file or install it in /etc/bash_completion.d/

_cluster_completions() {
  local cur prev
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"

  case "$prev" in
    cluster)
      COMPREPLY=($(compgen -W "status start stop restart members services help" -- "$cur"))
      ;;
    start|stop|restart)
      COMPREPLY=($(compgen -W "styx mnemosyne hermes themis atlas prometheus" -- "$cur"))
      ;;
    *)
      COMPREPLY=()
      ;;
  esac
}
complete -F _cluster_completions cluster
