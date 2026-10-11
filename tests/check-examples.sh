#!/bin/sh
# Non-empty check for the bOrca examples (spec item B7, amendments.md B7).
#
#   tests/check-examples.sh <path-to-cli>
#
# Runs every examples/**/*.orca outside examples/upstream/ through
# `cli --events -t 64 --seed 0` and requires each to do something in the
# bOrca dialect:
#
# - a file that contains an event operator glyph (!, %, :, = or ?) passes
#   when it prints at least one event line;
# - any other file passes when at least one tick's grid differs from the
#   file as loaded, which is the grid `cli -t 0 <file>` prints (it adds the
#   final newline some example files lack).
#
# The named exceptions below fail that rule on purpose; each is reported as
# EXCEPT with its reason. A listed exception that passes, or that names a
# file that does not exist, fails the run, so the list cannot go stale. A
# non-zero cli exit, a sanitizer report, a signal or a timeout fails the
# file, exception or not. examples/upstream/ is skipped: those patches need
# --dialect upstream (I1), and their goldens are XFAIL in tests/run.sh.
#
# Exit status: 0 when every file passes or is a listed exception that still
# fails the rule, 1 otherwise, 2 on usage.
set -u

# The named exceptions (Ian's decision, 2026-10-10): one per line, the path
# relative to the repository root, then its reason. They stay byte-identical
# to upstream Orca-c's examples.
exceptions='examples/basics/a.orca its A outputs already hold their sums, so no tick changes the grid
examples/basics/k.orca its K outputs already hold the variables, so no tick changes the grid
examples/basics/l.orca its L outputs already hold the lesser inputs, so no tick changes the grid
examples/misc/multiplication.orca its K and O outputs already hold their values, so no tick changes the grid
examples/misc/colors.orca its : is never banged (and is a locked $ input), so it sends nothing
examples/setups/sequencer.orca an empty template: its : are banged every tick, but every note input is empty, so it sends nothing'

root=$(cd "$(dirname "$0")/.." && pwd)
cli=${1-}
if [ -z "$cli" ] || [ ! -x "$cli" ] || [ "$#" != 1 ]; then
  echo "usage: $0 <path-to-cli>" >&2
  exit 2
fi
case $cli in
  /*) ;;
  *) cli="$PWD/$cli" ;;
esac

ticks=64
sanitizer_re='AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:'
# Make UBSan stop at its first report, so it fails the exit status as well.
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
export UBSAN_OPTIONS
timeout_cmd=
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd="timeout 60"
fi

tmpd=$(mktemp -d) || exit 2
trap 'rm -rf "$tmpd"' EXIT
trap 'exit 130' INT TERM
loaded="$tmpd/loaded"
events="$tmpd/events"
list="$tmpd/list"

n_files=0 n_pass=0 n_except=0 n_fail=0

# exception_reason <path>: prints the path's reason and returns 0 when the
# path is a named exception, returns 1 otherwise.
exception_reason() {
  printf '%s\n' "$exceptions" | awk -v p="$1" '
    $1 == p { sub(/^[^ ]+ /, ""); print; found = 1; exit }
    END { exit !found }'
}

# run_cli <output file> <args...>: runs cli, sets rc_crash when it crashed
# or reported a sanitizer error, rc_status to its exit status.
run_cli() {
  rc_out=$1
  shift
  # shellcheck disable=SC2086  # $timeout_cmd is a word list
  $timeout_cmd "$cli" "$@" >"$rc_out" 2>&1
  rc_status=$?
  rc_crash=
  if [ "$rc_status" -ge 124 ]; then
    rc_crash="cli exited $rc_status (signal or timeout)"
  elif grep -Eq "$sanitizer_re" "$rc_out"; then
    rc_crash="sanitizer report"
  elif [ "$rc_status" != 0 ]; then
    rc_crash="cli exited $rc_status"
  fi
}

# show_log <file>: prints the sanitizer lines of a failed run, or else its
# first lines, indented.
show_log() {
  if grep -Eq "$sanitizer_re" "$1"; then
    grep -E "$sanitizer_re" "$1" | head -n 3
  else
    head -n 3 "$1"
  fi | sed 's/^/       /'
}

# check_file <path relative to root>: sets cf_ok to 1 when the file passes
# the rule, 0 when it does not, and cf_what to what it found; sets cf_error,
# and cf_log to the output to show, when cli failed or its output could not
# be read.
check_file() {
  cf_error='' cf_ok=0 cf_what=''
  cf_log=$loaded
  run_cli "$loaded" -t 0 "$root/$1"
  if [ -n "$rc_crash" ]; then
    cf_error="cli -t 0: $rc_crash"
    return
  fi
  cf_log=$events
  run_cli "$events" --events -t "$ticks" --seed 0 "$root/$1"
  if [ -n "$rc_crash" ]; then
    cf_error="cli --events: $rc_crash"
    return
  fi
  # Reads the --events output tick by tick: event lines, then "t<n> GRID"
  # and exactly as many grid rows as the loaded grid has. Prints the number
  # of event lines and the first tick whose grid differs from the loaded
  # one (-1 for none), or "bad" when the output does not have that shape.
  cf_result=$(awk -v loaded="$loaded" -v ticks="$ticks" '
    BEGIN {
      h = 0
      while ((getline l < loaded) > 0) want[h++] = l
      tick = 0; row = -1; nev = 0; first = -1; bad = (h == 0)
    }
    row >= 0 {
      # Concatenation forces a string comparison: awk would compare rows
      # that look numeric, such as "1." and "01", as numbers.
      if (($0 "") != (want[row] "") && first < 0) first = tick
      if (++row == h) { row = -1; ++tick }
      next
    }
    $0 == ("t" tick " GRID") { row = 0; next }
    index($0, "t" tick " ") == 1 { ++nev; next }
    { bad = 1 }
    END {
      if (bad || row >= 0 || tick != ticks) print "bad"
      else print nev, first
    }' "$events")
  case $cf_result in
    bad | "")
      cf_error="unexpected --events output"
      return
      ;;
  esac
  cf_events=${cf_result% *}
  cf_first=${cf_result#* }
  if grep -q '[!%:=?]' "$root/$1"; then
    cf_what="$cf_events event lines"
    [ "$cf_events" -gt 0 ] && cf_ok=1
  else
    if [ "$cf_first" -ge 0 ]; then
      cf_what="grid changes at t$cf_first"
      cf_ok=1
    else
      cf_what="no grid change"
    fi
  fi
}

(cd "$root" && find examples -name '*.orca' ! -path 'examples/upstream/*' |
  LC_ALL=C sort) >"$list"
while IFS= read -r f; do
  n_files=$((n_files + 1))
  check_file "$f"
  if reason=$(exception_reason "$f"); then
    if [ -n "$cf_error" ]; then
      printf 'FAIL   %s (%s; listed exception)\n' "$f" "$cf_error"
      show_log "$cf_log"
      n_fail=$((n_fail + 1))
    elif [ "$cf_ok" = 1 ]; then
      printf 'FAIL   %s (listed exception, but it passes: %s; remove it from the list)\n' \
        "$f" "$cf_what"
      n_fail=$((n_fail + 1))
    else
      printf 'EXCEPT %s (%s: %s)\n' "$f" "$cf_what" "$reason"
      n_except=$((n_except + 1))
    fi
  elif [ -n "$cf_error" ]; then
    printf 'FAIL   %s (%s)\n' "$f" "$cf_error"
    show_log "$cf_log"
    n_fail=$((n_fail + 1))
  elif [ "$cf_ok" = 1 ]; then
    printf 'PASS   %s (%s)\n' "$f" "$cf_what"
    n_pass=$((n_pass + 1))
  else
    printf 'FAIL   %s (%s in %s ticks)\n' "$f" "$cf_what" "$ticks"
    n_fail=$((n_fail + 1))
  fi
done <"$list"

# A listed exception whose file is gone, or under examples/upstream/, fails.
printf '%s\n' "$exceptions" >"$list"
while read -r f _; do
  case $f in
    examples/upstream/*)
      printf 'FAIL   %s (listed exception under examples/upstream/, which is not checked)\n' "$f"
      n_fail=$((n_fail + 1))
      ;;
    *)
      if [ ! -f "$root/$f" ]; then
        printf 'FAIL   %s (listed exception, but no such file)\n' "$f"
        n_fail=$((n_fail + 1))
      fi
      ;;
  esac
done <"$list"

if [ "$n_files" = 0 ]; then
  echo "no examples found under $root/examples" >&2
  exit 1
fi
printf 'examples check: %s files, %s pass, %s exceptions, %s fail\n' \
  "$n_files" "$n_pass" "$n_except" "$n_fail"
[ "$n_fail" = 0 ]
