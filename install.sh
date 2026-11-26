#!/bin/sh
# sprig installer: the binary (checksum-verified, no Node needed), then
# connects each agent it finds: Claude Code, Codex, pi.
#
#   curl -fsSL https://raw.githubusercontent.com/AkhileshThykkat/sprig/main/install.sh | sh
#
# Options (after `sh -s --` when piped):
#   --yes          take every default, ask nothing
#   --link         keep everything in ~/.sprig and symlink it into place (default)
#   --copy         copy files straight into place, no ~/.sprig folder
#   --agents-only  skip the binary, just connect agents
#   --uninstall    remove what this script installed
# Environment: PREFIX (default ~/.local, binary goes in $PREFIX/bin),
# SPRIG_HOME (default ~/.sprig), SPRIG_BASE_URL (where releases come from).
set -eu

repo=AkhileshThykkat/sprig
base=${SPRIG_BASE_URL:-https://github.com/$repo/releases/latest/download}
sprig_home=${SPRIG_HOME:-$HOME/.sprig}
bin_dir=${PREFIX:-$HOME/.local}/bin
pi_dir=${PI_EXTENSIONS_DIR:-$HOME/.pi/agent/extensions}
yes=0 mode="" agents_only=0 uninstall=0

for arg in "$@"; do
  case $arg in
    --yes | -y) yes=1 ;;
    --link) mode=link ;;
    --copy) mode=copy ;;
    --agents-only) agents_only=1 ;;
    --uninstall) uninstall=1 ;;
    *) echo "sprig: unknown option $arg" >&2; exit 2 ;;
  esac
done

say() { printf '%s\n' "$*"; }
# ask "question" y|n: answers from the terminal even when this script is piped in.
ask() {
  if [ "$yes" = 1 ] || ! (: < /dev/tty) 2>/dev/null; then answer=$2; else
    [ "$2" = y ] && hint="Y/n" || hint="y/N"
    printf '%s [%s] ' "$1" "$hint" > /dev/tty
    read -r answer < /dev/tty || answer=""
    [ -n "$answer" ] || answer=$2
  fi
  case $answer in [Yy]*) return 0 ;; *) return 1 ;; esac
}
have() { command -v "$1" >/dev/null 2>&1; }

# Put $1 at $2, as a symlink or a copy.
place() {
  mkdir -p "$(dirname "$2")"
  rm -f "$2"
  if [ "$mode" = link ]; then ln -s "$1" "$2"; else install -m "${3:-644}" "$1" "$2"; fi
}

if [ "$uninstall" = 1 ]; then
  for f in "$bin_dir/sprig" "$pi_dir/sprig.ts"; do
    if [ -L "$f" ] || grep -qs "sprig" "$f"; then rm -f "$f" && say "removed $f"; fi
  done
  [ -d "$sprig_home" ] && rm -rf "$sprig_home" && say "removed $sprig_home"
  have claude && claude plugin uninstall sprig@sprig >/dev/null 2>&1 && say "removed the Claude Code plugin"
  have codex && codex plugin remove sprig@sprig >/dev/null 2>&1 && say "removed the Codex plugin"
  exit 0
fi

if [ -z "$mode" ]; then
  if ask "Keep sprig in its own folder ($sprig_home) and link it into place? (no: copy files directly)" y
  then mode=link; else mode=copy; fi
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Downloads release assets into $tmp and checks them against SHA256SUMS.
fetch() {
  curl -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS"
  if have sha256sum; then sum="sha256sum"; else sum="shasum -a 256"; fi
  for file in "$@"; do  # not $asset: sh has no locals, and the caller uses that name
    curl -fsSL "$base/$file" -o "$tmp/$file"
    (cd "$tmp" && grep " $file\$" SHA256SUMS | $sum -c - >/dev/null) || {
      echo "sprig: checksum mismatch for $file, not installing" >&2
      exit 1
    }
  done
}

# Where installed files live before they are placed: ~/.sprig for --link.
stage() {
  if [ "$mode" = link ]; then
    mkdir -p "$sprig_home/$(dirname "$2")"
    install -m "${3:-644}" "$1" "$sprig_home/$2"
    echo "$sprig_home/$2"
  else
    echo "$1"
  fi
}

# 1. The binary.
if [ "$agents_only" = 0 ]; then
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) asset=sprig-linux-x64 ;;
    Linux-aarch64 | Linux-arm64) asset=sprig-linux-arm64 ;;
    Darwin-*) asset=sprig-darwin-universal ;;
    *) echo "sprig: no prebuilt binary for $(uname -sm); build from source: https://github.com/$repo" >&2; exit 1 ;;
  esac
  fetch "$asset" sprig-pi.ts
  place "$(stage "$tmp/$asset" bin/sprig 755)" "$bin_dir/sprig" 755
  say "sprig installed: $bin_dir/sprig"
  case ":$PATH:" in *":$bin_dir:"*) ;; *) say "  add $bin_dir to your PATH" ;; esac
  pi_source="$tmp/sprig-pi.ts"
else
  pi_source=${SPRIG_PI_FILE:-}
fi

# 2. Agents.
found=0
if have claude; then
  found=1
  if ask "Connect Claude Code (installs the sprig plugin)?" y; then
    if claude plugin marketplace add "$repo" >/dev/null 2>&1 && claude plugin install sprig@sprig >/dev/null 2>&1
    then say "Claude Code: connected"
    else say "Claude Code: could not install; run: claude plugin install sprig --marketplace $repo"; fi
  fi
fi
if have codex; then
  found=1
  if ask "Connect Codex (installs the sprig plugin)?" y; then
    if codex plugin marketplace add "$repo" >/dev/null 2>&1 && codex plugin add sprig@sprig >/dev/null 2>&1
    then say "Codex: connected"
    else say "Codex: could not install; run: codex plugin marketplace add $repo && codex plugin add sprig@sprig"; fi
  fi
fi
if have pi; then
  found=1
  if ask "Connect pi (adds an extension)?" y; then
    if [ -z "$pi_source" ]; then
      fetch sprig-pi.ts
      pi_source="$tmp/sprig-pi.ts"
    fi
    if [ -e "$pi_dir/sprig.ts" ] && [ ! -L "$pi_dir/sprig.ts" ] && ! grep -qs "sprig for pi" "$pi_dir/sprig.ts"; then
      say "pi: $pi_dir/sprig.ts exists and is not ours; left alone"
    else
      place "$(stage "$pi_source" integrations/pi/sprig.ts)" "$pi_dir/sprig.ts"
      say "pi: connected ($pi_dir/sprig.ts)"
    fi
  fi
fi
[ "$found" = 1 ] || say "No agents found. Any tool can drive sprig: sprig emit busy"

say ""
say "Next: open a small terminal split and run: sprig    (or: sprig demo)"
