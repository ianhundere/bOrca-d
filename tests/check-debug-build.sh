#!/bin/sh
# Checks that a debug binary is sanitized and, given a unit-test runner's
# log, that the run was clean and ran every test the binary defines. Linux
# only. See tests/README.md.
#
#   tests/check-debug-build.sh <binary> [runner-log]
#
# The binary passes when its symbol table (nm, not nm -D) lists
# __asan_init and at least one __ubsan_handle_* symbol, each of class U
# (gcc's shared runtimes) or T (a runtime linked in statically, as clang
# does), so a debug build that lost ASan or UBSan fails: tool only warns
# when it cannot detect the compiler, and then adds no sanitizer flags.
# __ubsan_default_options, which the unit-test runner defines, never counts.
# A stripped binary lists no symbols and fails, so release and unsigned-char
# binaries, which link with -flto -s, are exempt and never checked. macOS
# prefixes symbol names with an underscore, so the script exits 2 on any
# system other than Linux, as tests/check-nm.sh does.
#
# Given the log of one run of build/debug/unit_tests, it also fails unless:
#   - the log ends its run with "<n> tests, 0 failed";
#   - no line reports a sanitizer (Sanitizer, or runtime error:), even one
#     printed after the summary, such as a LeakSanitizer report;
#   - every T test_* function the binary defines is reported, by
#     `ok <name>` or `FAIL <name>:`; the ones that are not are named, so a
#     test with its own prototype that no X-macro list registers fails;
#   - no test is reported twice (a test registered twice would otherwise
#     offset an unregistered one in the count);
#   - <n> equals the number of T test_* functions.
#
# Exit status: 0 when every check passes, 1 otherwise, naming what failed,
# 2 on usage or on a system other than Linux.
set -u

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: $0 <binary> [runner-log]" >&2
  exit 2
fi
if [ "$(uname -s)" != Linux ]; then
  echo "error: $0 is Linux/ELF only; nm's symbol names differ elsewhere" >&2
  exit 2
fi
bin=$1
log=${2-}
failed=0

fail() {
  printf 'FAIL  %s (%s)\n' "$bin" "$1"
  failed=1
}

if ! command -v nm >/dev/null 2>&1; then
  echo "error: nm not found" >&2
  exit 1
fi
if [ ! -f "$bin" ] || [ ! -r "$bin" ]; then
  fail "no such readable file"
  exit 1
fi

tmpdir=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpdir"' EXIT
trap 'exit 130' INT TERM
tmp=$tmpdir/symbols

if ! LC_ALL=C nm -P "$bin" >"$tmpdir/nm" 2>"$tmpdir/err"; then
  fail "nm cannot read it: $(head -n 1 "$tmpdir/err")"
  exit 1
fi
# "<name> <class>" per symbol, with any @version suffix dropped.
awk '{ sub(/@.*/, "", $1); print $1, $2 }' "$tmpdir/nm" >"$tmp"
if ! [ -s "$tmp" ]; then
  fail "nm lists no symbols: stripped, or a release build"
  exit 1
fi

if ! awk '$1 == "__asan_init" && ($2 == "U" || $2 == "T") { f = 1 } END { exit !f }' "$tmp"; then
  fail "no __asan_init of class U or T: not built with ASan"
fi
if ! awk '$1 ~ /^__ubsan_handle_/ && ($2 == "U" || $2 == "T") { f = 1 } END { exit !f }' "$tmp"; then
  fail "no __ubsan_handle_* symbol of class U or T: not built with UBSan"
fi
sanitizers="__asan_init, __ubsan_handle_*"

if [ -z "$log" ]; then
  if [ "$failed" = 0 ]; then
    printf 'PASS  %s (%s)\n' "$bin" "$sanitizers"
  fi
  exit "$failed"
fi

if [ ! -f "$log" ] || [ ! -r "$log" ]; then
  fail "no readable runner log $log"
  exit 1
fi
summary=$(grep -E '^[0-9]+ tests, [0-9]+ failed$' "$log" | tail -n 1)
ran=${summary%% *}
if [ -z "$summary" ]; then
  fail "$log has no \"<n> tests, <m> failed\" line: the runner did not finish"
elif [ "$summary" != "$ran tests, 0 failed" ]; then
  fail "the runner reported \"$summary\""
fi
if grep -Eq 'Sanitizer|runtime error:' "$log"; then
  fail "$log holds a sanitizer report: $(grep -E 'Sanitizer|runtime error:' "$log" | head -n 1)"
fi
# Each reported test once: an ok line, or a run of FAIL lines (one per
# failed check). Prints "repeat <name>" for a test reported twice and
# "unrun <test_name>" for a defined test the log never reports.
awk '
  NR == FNR {
    if ($0 ~ /^ok /) { name = substr($0, 4); runs[name]++; prev = ""; next }
    if ($0 ~ /^FAIL [^:]+:/) {
      name = substr($0, 6); sub(/:.*/, "", name)
      if (name != prev) runs[name]++
      prev = name
      next
    }
    prev = ""
    next
  }
  $2 == "T" && $1 ~ /^test_/ && !(substr($1, 6) in runs) { print "unrun", $1 }
  END { for (name in runs) if (runs[name] > 1) print "repeat", name }
' "$log" "$tmp" | LC_ALL=C sort >"$tmpdir/report"
unrun=$(awk '$1 == "unrun" { printf "%s%s", (n++ ? ", " : ""), $2 }' "$tmpdir/report")
repeat=$(awk '$1 == "repeat" { printf "%s%s", (n++ ? ", " : ""), $2 }' "$tmpdir/report")
defined=$(awk '$2 == "T" && $1 ~ /^test_/ { n++ } END { print n + 0 }' "$tmp")
if [ -n "$unrun" ]; then
  fail "never run: $unrun"
fi
if [ -n "$repeat" ]; then
  fail "reported more than once: $repeat"
fi
if [ -n "$summary" ] && [ "$ran" != "$defined" ]; then
  fail "the runner ran $ran tests but the binary defines $defined T test_* functions"
fi
if [ "$failed" = 0 ]; then
  printf 'PASS  %s (%s; %s tests run, %s T test_* defined)\n' \
    "$bin" "$sanitizers" "$ran" "$defined"
fi
exit "$failed"
