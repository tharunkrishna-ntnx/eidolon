# Bash completion for the 'misc-ctl' CLI tool.
# Source this file on the master user's shell.

_misc_ctl_completions() {
  local cur prev pprev
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
  pprev="${COMP_WORDS[COMP_CWORD-2]}"

  case "$prev" in
    misc-ctl)
      COMPREPLY=($(compgen -W "scenarios status" -- "$cur"))
      ;;
    scenarios)
      COMPREPLY=($(compgen -W "list info apply hint verify reset" -- "$cur"))
      ;;
    info|apply|hint|verify)
      local repo_root
      repo_root="$(cd "$(dirname "$(which misc-ctl 2>/dev/null || echo .)")/.." 2>/dev/null && pwd)"
      if [ ! -d "$repo_root/misc/scenarios" ]; then
        repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." 2>/dev/null && pwd)"
      fi
      if [ -d "$repo_root/misc/scenarios" ]; then
        local scenarios
        scenarios=$(ls -1 "$repo_root/misc/scenarios/" 2>/dev/null)
        COMPREPLY=($(compgen -W "$scenarios" -- "$cur"))
      fi
      ;;
    *)
      COMPREPLY=()
      ;;
  esac
}
complete -F _misc_ctl_completions misc-ctl
