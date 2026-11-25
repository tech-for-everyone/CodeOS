#!/bin/sh
# cora host test — builds the compiler, translates the example programs,
# compiles the generated C with -Werror, and checks the executed output.
#
#   sh pkgs/core/cora/tests/run.sh

set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src="$here/../src"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }
fail=0

gcc -std=c11 -O1 -Wall -Wextra -Werror -o "$tmp/cora" "$src/cora.c"

echo "cora host test"

# 1. The user's example: print("Hello World").
"$tmp/cora" "$src/hello.co" "$tmp/hello.c"
grep -Fq '#include <stdio.h>' "$tmp/hello.c" \
    && pass "generated C uses only <stdio.h>" || bad "missing <stdio.h>"
grep -Fq 'printf("%s\n", "Hello World")' "$tmp/hello.c" \
    && pass "print() becomes a printf call" || bad "print() translation wrong"
gcc -std=c11 -Wall -Wextra -Werror "$tmp/hello.c" -o "$tmp/hello"
out=$("$tmp/hello")
[ "$out" = "Hello World" ] && pass "hello.co prints Hello World" \
                           || bad "hello output was: $out"

# 2. Functions, branches, loops, logic and arithmetic end to end.
"$tmp/cora" "$src/selftest.co" "$tmp/selftest.c"
gcc -std=c11 -Wall -Wextra -Werror "$tmp/selftest.c" -o "$tmp/selftest"
out=$("$tmp/selftest")
expected=$(printf 'add = 5\nbranch: five\nfib(10) = 55\nlogic ok')
[ "$out" = "$expected" ] && pass "selftest.co: functions, if/elif/else, while, and" \
                          || bad "selftest output was: $out"

# 3. With no output argument, INPUT.co becomes INPUT.c.
cp "$src/hello.co" "$tmp/default.co"
(cd "$tmp" && ./cora default.co)
[ -f "$tmp/default.c" ] && pass "default output name replaces .co with .c" \
                         || bad "default output was not written"

# 4. Comments and single-quoted strings.
printf '# leading comment\nprint(\x27hi\x27) # trailing comment\n' > "$tmp/quotes.co"
"$tmp/cora" "$tmp/quotes.co" "$tmp/quotes.c"
gcc -std=c11 -Wall -Wextra -Werror "$tmp/quotes.c" -o "$tmp/quotes"
[ "$("$tmp/quotes")" = "hi" ] && pass "comments and single quotes work" \
                               || bad "comment/quote test failed"

# 5. Tabs are rejected with a useful message rather than mis-parsed.
printf 'if True:\n\tprint("x")\n' > "$tmp/tabs.co"
set +e
"$tmp/cora" "$tmp/tabs.co" "$tmp/tabs.c" > "$tmp/tabs.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "tab indentation exits non-zero" || bad "tabs were accepted"
grep -q 'line 2: tabs are not indentation' "$tmp/tabs.log" \
    && pass "tab error names the line" || bad "wrong tab error: $(cat "$tmp/tabs.log")"

# 6. A header without ':' is a syntax error, not silently generated C.
printf 'if True\n    print("x")\n' > "$tmp/colon.co"
set +e
"$tmp/cora" "$tmp/colon.co" "$tmp/colon.c" > "$tmp/colon.log" 2>&1; rc=$?
set -e
[ "$rc" != 0 ] && pass "missing colon exits non-zero" || bad "missing colon accepted"
grep -q 'line 1: malformed if statement' "$tmp/colon.log" \
    && pass "syntax error names the line" || bad "wrong error: $(cat "$tmp/colon.log")"

if [ "$fail" = 0 ]; then echo "cora: all checks passed"; else echo "cora: FAILURES"; fi
exit "$fail"
