#!/bin/sh
# Runs install.sh against a fake release folder, with stub agents on PATH, in
# a throwaway HOME: nothing on the real machine is touched.
# Usage: sh tests/install_test.sh path/to/sprig
set -eu
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
root=$(cd "$(dirname "$0")/.." && pwd)
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
fail() { echo "install_test: $*" >&2; exit 1; }

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) asset=sprig-linux-x64 ;;
  Linux-aarch64 | Linux-arm64) asset=sprig-linux-arm64 ;;
  Darwin-*) asset=sprig-darwin-universal ;;
esac
mkdir -p "$dir/release" "$dir/stubs"
cp "$bin" "$dir/release/$asset"
cp "$root/integrations/pi/sprig.ts" "$dir/release/sprig-pi.ts"
(cd "$dir/release" && sha256sum "$asset" sprig-pi.ts > SHA256SUMS)

# Stub agents record what the installer asked of them.
for agent in claude codex pi; do
  printf '#!/bin/sh\necho "%s $*" >> "%s/calls"\n' "$agent" "$dir" > "$dir/stubs/$agent"
  chmod +x "$dir/stubs/$agent"
done

run() {
  env -i HOME="$dir/home" PATH="$dir/stubs:/usr/bin:/bin" SPRIG_BASE_URL="file://$dir/release" \
    sh "$root/install.sh" --yes "$@" < /dev/null
}

# --link (default): everything in ~/.sprig, symlinked into place.
run > "$dir/out"
[ -L "$dir/home/.local/bin/sprig" ] || fail "binary not linked"
[ -x "$dir/home/.sprig/bin/sprig" ] || fail "no binary in ~/.sprig"
[ -L "$dir/home/.pi/agent/extensions/sprig.ts" ] || fail "pi extension not linked"
"$dir/home/.local/bin/sprig" help > /dev/null || fail "installed binary does not run"
grep -q "claude plugin marketplace add AkhileshThykkat/sprig" "$dir/calls" || fail "Claude Code not set up"
grep -q "claude plugin install sprig@sprig" "$dir/calls" || fail "Claude plugin not installed"
grep -q "codex plugin add sprig@sprig" "$dir/calls" || fail "Codex plugin not installed"

# --uninstall removes it all.
run --uninstall > /dev/null
[ ! -e "$dir/home/.local/bin/sprig" ] && [ ! -e "$dir/home/.sprig" ] || fail "uninstall left files"
[ ! -e "$dir/home/.pi/agent/extensions/sprig.ts" ] || fail "uninstall left the pi extension"

# --copy: real files, no ~/.sprig.
run --copy > /dev/null
[ -f "$dir/home/.local/bin/sprig" ] && [ ! -L "$dir/home/.local/bin/sprig" ] || fail "binary not copied"
[ ! -e "$dir/home/.sprig" ] || fail "--copy made ~/.sprig"

# Someone else's pi extension called sprig.ts is never overwritten.
echo "// mine" > "$dir/home/.pi/agent/extensions/sprig.ts"
run --copy > /dev/null
grep -q "// mine" "$dir/home/.pi/agent/extensions/sprig.ts" || fail "overwrote a foreign pi extension"

# A tampered download is refused.
echo tampered >> "$dir/release/$asset"
rm -rf "$dir/home"
if run > /dev/null 2>&1; then fail "installed a binary with a bad checksum"; fi
[ ! -e "$dir/home/.local/bin/sprig" ] || fail "bad binary was placed"

echo "install_test ok"
