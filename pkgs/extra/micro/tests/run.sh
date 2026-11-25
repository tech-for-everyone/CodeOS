#!/bin/sh
# micro host test — builds the real editor with the shared CodeOS fs shim and
# drives it through piped keystrokes. Output files are checked byte for byte;
# the screen dump is only used to assert the unsaved-changes warning.
#
#   sh pkgs/extra/micro/tests/run.sh

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
shim="$here/../../../ports/tests/codeos_fs_shim.h"
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }
fail=0

gcc -std=c11 -O1 -Wall -Wextra -Werror -o "$tmp/micro" -include "$shim" "$src/micro.c"

echo "micro port host test"

# 1. Type into a new file, save with Ctrl-S, quit with Ctrl-Q.
printf 'hello micro\023\021' | "$tmp/micro" "$tmp/new.txt" > "$tmp/screen1"
python3 - "$tmp/new.txt" <<'PY' && pass "typing, Ctrl-S and Ctrl-Q save the buffer" || bad "new file not saved"
import sys
assert open(sys.argv[1], 'rb').read() == b'hello micro\n'
PY

# 2. Editing an existing file at the start must not lose the rest of it.
printf 'abc\n' > "$tmp/existing.txt"
printf '\033[HX\023\021' | "$tmp/micro" "$tmp/existing.txt" > /dev/null
python3 - "$tmp/existing.txt" <<'PY' && pass "insert at start preserves the rest of the line" || bad "insert lost characters"
import sys
assert open(sys.argv[1], 'rb').read() == b'Xabc\n'
PY

# 3. Enter splits a line; the saved file ends with a newline.
printf 'one\rtwo\023\021' | "$tmp/micro" "$tmp/two.txt" > /dev/null
python3 - "$tmp/two.txt" <<'PY' && pass "Enter splits lines and the file ends with a newline" || bad "line split wrong"
import sys
assert open(sys.argv[1], 'rb').read() == b'one\ntwo\n'
PY

# 4. Backspace and Ctrl-K edit in place.
printf 'abcd\177\023\021' | "$tmp/micro" "$tmp/back.txt" > /dev/null
python3 - "$tmp/back.txt" <<'PY' && pass "Backspace removes one character" || bad "Backspace failed"
import sys
assert open(sys.argv[1], 'rb').read() == b'abc\n'
PY
printf 'abcdef\033[H\013\023\021' | "$tmp/micro" "$tmp/kill.txt" > /dev/null
python3 - "$tmp/kill.txt" <<'PY' && pass "Ctrl-K deletes to the end of the line" || bad "Ctrl-K failed"
import sys
assert open(sys.argv[1], 'rb').read() == b'\n'
PY

# 5. Tab inserts two spaces (micro's soft indent).
printf '\tX\023\021' | "$tmp/micro" "$tmp/tab.txt" > /dev/null
python3 - "$tmp/tab.txt" <<'PY' && pass "Tab inserts a two-space indent" || bad "Tab failed"
import sys
assert open(sys.argv[1], 'rb').read() == b'  X\n'
PY

# 6. One Ctrl-Q with unsaved changes warns and does not write anything.
printf 'x\021\021' | "$tmp/micro" "$tmp/unsaved.txt" > "$tmp/screen6"
grep -q "Unsaved changes" "$tmp/screen6" \
    && pass "first Ctrl-Q warns about unsaved changes" || bad "no unsaved warning"
[ ! -e "$tmp/unsaved.txt" ] \
    && pass "unsaved buffer was not written" || bad "unsaved buffer was written"

# 7. Ctrl-O prompts for a path, opens it, and edits are saved there.
printf 'seed\n' > "$tmp/opened.txt"
printf '\017%s\r\033[HZ\023\021' "$tmp/opened.txt" \
    | "$tmp/micro" > "$tmp/screen7"
python3 - "$tmp/opened.txt" "$tmp/screen7" <<'PY' && pass "Ctrl-O opens a file and saves edits to it" || bad "Ctrl-O failed"
import sys
assert open(sys.argv[1], 'rb').read() == b'Zseed\n'
assert 'opened' in open(sys.argv[2]).read()
PY

if [ "$fail" = 0 ]; then echo "micro: all checks passed"; else echo "micro: FAILURES"; fi
exit "$fail"
