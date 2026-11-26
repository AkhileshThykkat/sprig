#!/bin/sh
# End-to-end checks for the sprig command: sending, hooks, the viewer.
# Usage: sh tests/cli_test.sh path/to/sprig
set -eu
bin=$1
dir=$(mktemp -d)
trap 'exec 3>&- 2>/dev/null; rm -rf "$dir"' EXIT
fail() { echo "cli_test: $*" >&2; exit 1; }
in_pty() { python3 -c 'import pty, sys; pty.spawn(sys.argv[1:])' "$@"; }

export SPRIG_PIPE="$dir/pipe"

# Nobody listening (no pipe, then a pipe with no reader): succeed at once.
"$bin" emit busy
echo '{"hook_event_name":"Stop"}' | "$bin" hook
mkfifo "$SPRIG_PIPE"
"$bin" emit busy

# A reader: moods arrive in order, unknown events send nothing.
exec 3<>"$SPRIG_PIPE"
"$bin" emit thinking
echo '{"hook_event_name":"SomethingNew"}' | "$bin" hook
echo '{"hook_event_name":"PreToolUse"}' | "$bin" hook
read -r first <&3
read -r second <&3
[ "$first" = thinking ] && [ "$second" = busy ] || fail "got '$first' '$second'"
exec 3>&-

if "$bin" emit nonsense 2>/dev/null; then fail "accepted an unknown mood"; fi
[ "$(echo '{}' | "$bin" hook)" = "" ] || fail "hook printed output"

# A regular file where the pipe should be is never written to.
rm "$SPRIG_PIPE"
: > "$SPRIG_PIPE"
"$bin" emit busy
[ ! -s "$SPRIG_PIPE" ] || fail "wrote into a regular file"
rm "$SPRIG_PIPE"

# The viewer draws, takes events, and shuts down cleanly on Ctrl-C, under a
# pseudo-terminal. Sanitizer reports (leaks included) land in the log.
(sleep 0.5 && "$bin" emit happy && sleep 0.3 && "$bin" emit error) &
in_pty timeout -s INT 2 "$bin" > "$dir/viewer.log" 2>&1 || true
wait
grep -q "sprig" "$dir/viewer.log" || fail "viewer drew nothing"
grep -q "error" "$dir/viewer.log" || fail "viewer missed an event"
! grep -q "Sanitizer" "$dir/viewer.log" || { cat "$dir/viewer.log"; fail "sanitizer report"; }

in_pty timeout -s INT 1 "$bin" demo > "$dir/demo.log" 2>&1 || true
! grep -q "Sanitizer" "$dir/demo.log" || { cat "$dir/demo.log"; fail "sanitizer report"; }

echo "cli_test ok"
