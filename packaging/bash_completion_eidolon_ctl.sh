# Bash completion for the 'eidolon-ctl' CLI tool.
# Source this file on the master user's shell.

_eidolon_ctl_completions() {
  local cur prev pprev
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
  pprev="${COMP_WORDS[COMP_CWORD-2]}"

  case "$prev" in
    eidolon-ctl)
      COMPREPLY=($(compgen -W "scenarios cluster oracle" -- "$cur"))
      ;;
    scenarios)
      COMPREPLY=($(compgen -W "list info apply hint verify reset" -- "$cur"))
      ;;
    oracle)
      COMPREPLY=($(compgen -W "status profile reset profiles" -- "$cur"))
      ;;
    cluster)
      COMPREPLY=($(compgen -W "status" -- "$cur"))
      ;;
    info|apply|hint|verify)
      # Complete with scenario IDs from the scenarios directory.
      local repo_root
      repo_root="$(cd "$(dirname "$(which eidolon-ctl 2>/dev/null || echo .)")/.." 2>/dev/null && pwd)"
      if [ -d "$repo_root/scenarios" ]; then
        local scenarios
        scenarios=$(ls -1 "$repo_root/scenarios/" 2>/dev/null)
        COMPREPLY=($(compgen -W "$scenarios" -- "$cur"))
      fi
      ;;
    profile)
      COMPREPLY=($(compgen -W "steady burst sequential_heavy idle" -- "$cur"))
      ;;
    *)
      COMPREPLY=()
      ;;
  esac
}
complete -F _eidolon_ctl_completions eidolon-ctl
