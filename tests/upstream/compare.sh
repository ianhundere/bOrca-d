#!/bin/sh
# The upstream dialect's byte-identity check against Orca-c 9df9786 (spec
# item I1, CAP-13; architecture spine AD-21).
#
#   tests/upstream/compare.sh <path-to-bOrca-cli>
#
# Needs the Orca-c tree and cli that tests/upstream/build-cli.sh builds
# (build/upstream/orca-c/ and build/upstream/cli). It checks, reporting every
# failure and carrying on:
#
# 1. events_print.c and events_print.h in the Orca-c tree are this
#    repository's, so both cli print events through one file.
# 2. Every examples/**/*.orca of the Orca-c tree (43 at 9df9786): Orca-c's
#    `cli --events -t 96 --seed 0` and bOrca's, with `--dialect upstream`
#    added, print byte-identical event lines and grids, and both exit 0 with
#    no sanitizer report. Fewer than 43 files fails the run.
# 3. Each examples/upstream/<rel>.orca is Orca-c's examples/<rel>.orca, and
#    its golden, tests/expected/examples/upstream/<rel>.events, is byte for
#    byte what Orca-c's cli prints for it, so neither can drift from Orca-c,
#    and holds at least one event line: the proof that the patch does
#    something in the upstream dialect (CAP-13).
# 4. The six examples that tests/check-examples.sh names as exceptions are
#    Orca-c's files at the same paths. The list below must name exactly the
#    paths of check-examples.sh's list, in its order.
#
# Exit status: 0 when every check passes, 1 otherwise, 2 on usage or when
# the Orca-c build is missing.
set -u

ticks=96
min_examples=43
# tests/check-examples.sh's named exceptions (amendments.md B7), which stay
# byte-identical to Orca-c's examples. Check 4 fails unless they are the
# paths of that script's list, so the two lists change together.
exceptions='examples/basics/a.orca
examples/basics/k.orca
examples/basics/l.orca
examples/misc/multiplication.orca
examples/misc/colors.orca
examples/setups/sequencer.orca'

root=$(cd "$(dirname "$0")/../.." && pwd)
cli=${1-}
if [ -z "$cli" ] || [ ! -x "$cli" ] || [ "$#" != 1 ]; then
  echo "usage: $0 <path-to-bOrca-cli>" >&2
  exit 2
fi
case $cli in
  /*) ;;
  *) cli="$PWD/$cli" ;;
esac
up_cli="$root/build/upstream/cli"
tree="$root/build/upstream/orca-c"
if [ ! -x "$up_cli" ] || [ ! -d "$tree/examples" ]; then
  echo "no Orca-c build in $root/build/upstream: run tests/upstream/build-cli.sh first" >&2
  exit 2
fi

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
list="$tmpd/list"
up_out="$tmpd/orca-c.out"
up_err="$tmpd/orca-c.err"
bo_out="$tmpd/borca.out"
bo_err="$tmpd/borca.err"

n_fail=0

fail() {
  printf 'FAIL  %s\n' "$1"
  n_fail=$((n_fail + 1))
}

# run <stdout file> <stderr file> <cli> <args...>: runs a cli and sets
# run_error to why the run failed (a non-zero exit, a signal, a timeout or a
# sanitizer report), or to nothing.
run() {
  r_out=$1 r_err=$2
  shift 2
  # shellcheck disable=SC2086  # $timeout_cmd is a word list
  $timeout_cmd "$@" >"$r_out" 2>"$r_err"
  r_status=$?
  run_error=
  if grep -Eq "$sanitizer_re" "$r_out" "$r_err"; then
    run_error="sanitizer report"
  elif [ "$r_status" -ge 124 ]; then
    run_error="exited $r_status (signal or timeout)"
  elif [ "$r_status" != 0 ]; then
    run_error="exited $r_status"
  fi
}

# indent <file>: prints the first lines of a file, indented.
indent() {
  head -n 6 "$1" | sed 's/^/      /'
}

# 1. One events printer.
n_files=0 n_files_ok=0
for f in events_print.c events_print.h; do
  n_files=$((n_files + 1))
  if cmp -s "$root/$f" "$tree/$f"; then
    printf 'PASS  %s is the Orca-c build'"'"'s copy\n' "$f"
    n_files_ok=$((n_files_ok + 1))
  else
    fail "$f differs from $tree/$f"
  fi
done

# 2. Orca-c's examples, through both cli.
n_examples=0 n_examples_ok=0
(cd "$tree" && find examples -name '*.orca' | LC_ALL=C sort) >"$list"
while IFS= read -r rel; do
  n_examples=$((n_examples + 1))
  run "$up_out" "$up_err" "$up_cli" --events -t "$ticks" --seed 0 "$tree/$rel"
  if [ -n "$run_error" ]; then
    fail "$rel (Orca-c cli $run_error)"
    indent "$up_err"
    continue
  fi
  run "$bo_out" "$bo_err" "$cli" --events -t "$ticks" --seed 0 \
    --dialect upstream "$tree/$rel"
  if [ -n "$run_error" ]; then
    fail "$rel (bOrca cli $run_error)"
    indent "$bo_err"
    continue
  fi
  if [ ! -s "$up_out" ]; then
    fail "$rel (Orca-c cli printed nothing)"
  elif cmp -s "$up_out" "$bo_out"; then
    printf 'PASS  %s\n' "$rel"
    n_examples_ok=$((n_examples_ok + 1))
  else
    fail "$rel (output differs; - Orca-c, + bOrca --dialect upstream)"
    diff -u "$up_out" "$bo_out" >"$tmpd/diff"
    sed -n '3,$p' "$tmpd/diff" | head -n 12 | sed 's/^/      /'
  fi
done <"$list"
if [ "$n_examples" -lt "$min_examples" ]; then
  fail "only $n_examples Orca-c examples ran, expected at least $min_examples"
fi

# 3. examples/upstream/ and its goldens.
n_goldens=0 n_goldens_ok=0
(cd "$root" && find examples/upstream -name '*.orca' | LC_ALL=C sort) >"$list"
while IFS= read -r f; do
  rel=${f#examples/upstream/}
  n_files=$((n_files + 1))
  if cmp -s "$root/$f" "$tree/examples/$rel"; then
    printf 'PASS  %s is Orca-c'"'"'s examples/%s\n' "$f" "$rel"
    n_files_ok=$((n_files_ok + 1))
  else
    fail "$f differs from Orca-c's examples/$rel"
  fi
  n_goldens=$((n_goldens + 1))
  golden="tests/expected/examples/upstream/${rel%.orca}.events"
  if [ ! -f "$root/$golden" ]; then
    fail "$golden (missing)"
    continue
  fi
  run "$up_out" "$up_err" "$up_cli" --events -t "$ticks" --seed 0 "$root/$f"
  if [ -n "$run_error" ]; then
    fail "$golden (Orca-c cli $run_error)"
    indent "$up_err"
  elif ! cmp -s "$up_out" "$root/$golden"; then
    fail "$golden differs from Orca-c's output"
  elif ! grep -E '^t[0-9]+ ' "$root/$golden" | grep -Evq '^t[0-9]+ GRID$'; then
    fail "$golden holds no event line"
  else
    printf 'PASS  %s is Orca-c'"'"'s output, with event lines\n' "$golden"
    n_goldens_ok=$((n_goldens_ok + 1))
  fi
done <"$list"
if [ "$n_goldens" = 0 ]; then
  fail "no examples under examples/upstream/"
fi

# 4. The named exceptions: first the list against check-examples.sh's (the
# first word of each line of its exceptions='...' block), then each file.
listed="$tmpd/listed"
sed -n "/^exceptions='/,/'\$/p" "$root/tests/check-examples.sh" |
  sed "s/^exceptions='//" | cut -d' ' -f1 >"$listed"
printf '%s\n' "$exceptions" >"$list"
if cmp -s "$list" "$listed"; then
  printf 'PASS  the exceptions are tests/check-examples.sh'"'"'s\n'
else
  fail "the exceptions here differ from tests/check-examples.sh's (- here, + there)"
  diff -u "$list" "$listed" | sed -n '3,$p' | sed 's/^/      /'
fi
while IFS= read -r f; do
  n_files=$((n_files + 1))
  if cmp -s "$root/$f" "$tree/$f"; then
    printf 'PASS  %s is Orca-c'"'"'s\n' "$f"
    n_files_ok=$((n_files_ok + 1))
  else
    fail "$f differs from Orca-c's $f"
  fi
done <"$list"

printf 'upstream check: %s examples, %s identical; %s goldens, %s match; %s files, %s match; %s fail\n' \
  "$n_examples" "$n_examples_ok" "$n_goldens" "$n_goldens_ok" \
  "$n_files" "$n_files_ok" "$n_fail"
[ "$n_fail" = 0 ]
