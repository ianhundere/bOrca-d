#!/bin/sh
# Golden tests for bOrca's cli. See tests/README.md for the layout and rules.
#
#   tests/run.sh [--update] <path-to-cli> [name-filter]
#
# Every examples/**/*.orca and tests/patches/*.orca runs through
# `cli --events -t 64 --seed 0 --dialect borca` (a <name>.args sidecar beside
# the expected file appends flags; later flags win) and its output is compared
# byte for byte with tests/expected/<examples|patches>/<name>.events.
#
# A <name>.xfail sidecar names the one spec item whose fix the case still
# waits for: exactly one id matching ^(B[1-8]|I[1-6])$, so a repro that waits
# for two items is two cases. An output mismatch is then XFAIL and does not
# fail the run, while an unexpected pass (XPASS) does, so the commit that
# lands the item deletes the marker. A marker that is empty, holds anything
# but one such id, or names two ids fails the case as a bad marker, in both
# modes. A marker never hides a crash: a sanitizer report, a signal or a
# timeout fails the run whatever the marker says.
#
# --update rewrites expected files from the current output and skips cases
# that carry a marker, valid or bad, so hand-written fixed-behaviour files
# are never overwritten by accident. Expected files or sidecars whose input
# no longer exists are reported as ORPHAN and fail the run.
#
# Exit status: 0 when every case is PASS or XFAIL and nothing is orphaned,
# 1 otherwise, 2 on usage.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
update=0
if [ "${1-}" = "--update" ]; then
  update=1
  shift
fi
cli=${1-}
filter=${2-}
if [ -z "$cli" ] || [ ! -x "$cli" ]; then
  echo "usage: $0 [--update] <path-to-cli> [name-filter]" >&2
  exit 2
fi
case $cli in
  /*) ;;
  *) cli="$PWD/$cli" ;;
esac

default_args="-t 64 --seed 0 --dialect borca"
sanitizer_re='AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:'
# Make UBSan stop at its first report, so it fails the exit status as well.
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
export UBSAN_OPTIONS
timeout_cmd=
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd="timeout 60"
fi

tmp=$(mktemp) || exit 2
list=$(mktemp) || exit 2
trap 'rm -f "$tmp" "$list"' EXIT
trap 'exit 130' INT TERM

n_total=0 n_pass=0 n_fail=0 n_xfail=0 n_xpass=0 n_updated=0 n_skipped=0 n_orphan=0

# expected_base <input path relative to root>: the expected path without its
# extension, relative to root.
expected_base() {
  case $1 in
    examples/*)
      eb=${1#examples/}
      printf 'tests/expected/examples/%s' "${eb%.orca}"
      ;;
    tests/patches/*)
      eb=${1#tests/patches/}
      printf 'tests/expected/patches/%s' "${eb%.orca}"
      ;;
  esac
}

# read_marker <.xfail path relative to root>: sets rc_xfail to the marker's
# item id, or rc_bad to why it is not exactly one valid id.
read_marker() {
  rm_out=$(LC_ALL=C awk '
    { for (i = 1; i <= NF; i++) { n++; w = w (n > 1 ? " " : "") $i } }
    END {
      gsub(/[^ -~]/, "?", w)
      if (n == 0) print "bad holds no id"
      else if (n > 1) printf "bad names %d ids, %s\n", n, w
      else if (w !~ /^(B[1-8]|I[1-6])$/) printf "bad holds %s, not an item id\n", w
      else print "ok " w
    }' "$root/$1" 2>&1)
  case $rm_out in
    "ok "*) rc_xfail=${rm_out#ok } ;;
    "bad "*) rc_bad=${rm_out#bad } ;;
    *) rc_bad="cannot be read" ;;
  esac
}

# run_case <input path relative to root>
run_case() {
  rc_in=$1
  case $rc_in in
    *"$filter"*) ;;
    *) return ;;
  esac
  rc_base=$(expected_base "$rc_in")
  rc_exp="$root/$rc_base.events"
  rc_args=$default_args
  if [ -f "$root/$rc_base.args" ]; then
    rc_args="$rc_args $(cat "$root/$rc_base.args")"
  fi
  rc_xfail=
  rc_bad=
  if [ -f "$root/$rc_base.xfail" ]; then
    read_marker "$rc_base.xfail"
  fi
  n_total=$((n_total + 1))
  if [ -n "$rc_bad" ]; then
    rc_note=
    if [ "$update" = 1 ]; then
      rc_note="; not updated"
    fi
    printf 'FAIL  %s (bad marker: %s %s; it must hold one item id, B1-B8 or I1-I6%s)\n' \
      "$rc_in" "$rc_base.xfail" "$rc_bad" "$rc_note"
    n_fail=$((n_fail + 1))
    return
  fi

  # shellcheck disable=SC2086  # $timeout_cmd and $rc_args are word lists
  $timeout_cmd "$cli" --events $rc_args "$root/$rc_in" >"$tmp" 2>&1
  rc_status=$?
  rc_crash=
  if [ "$rc_status" -ge 124 ]; then
    rc_crash="cli exited $rc_status (signal or timeout)"
  elif grep -Eq "$sanitizer_re" "$tmp"; then
    rc_crash="sanitizer report"
  fi

  if [ "$update" = 1 ]; then
    if [ -n "$rc_xfail" ]; then
      printf 'skip  %s (xfail %s, not updated)\n' "$rc_in" "$rc_xfail"
      n_skipped=$((n_skipped + 1))
    elif [ -n "$rc_crash" ] || [ "$rc_status" != 0 ]; then
      printf 'FAIL  %s (%s; not updated)\n' "$rc_in" \
        "${rc_crash:-cli exited $rc_status}"
      n_fail=$((n_fail + 1))
    else
      mkdir -p "$(dirname "$rc_exp")"
      cp "$tmp" "$rc_exp"
      printf 'wrote %s\n' "$rc_base.events"
      n_updated=$((n_updated + 1))
    fi
    return
  fi

  if [ -n "$rc_crash" ]; then
    printf 'FAIL  %s (%s)\n' "$rc_in" "$rc_crash"
    grep -E "$sanitizer_re" "$tmp" | head -n 3 | sed 's/^/      /'
    n_fail=$((n_fail + 1))
    return
  fi
  if [ ! -f "$rc_exp" ]; then
    printf 'FAIL  %s (no expected file; run with --update)\n' "$rc_in"
    n_fail=$((n_fail + 1))
    return
  fi
  if [ "$rc_status" = 0 ] && cmp -s "$tmp" "$rc_exp"; then
    if [ -n "$rc_xfail" ]; then
      printf 'XPASS %s (marked xfail for %s: delete %s)\n' \
        "$rc_in" "$rc_xfail" "$rc_base.xfail"
      n_xpass=$((n_xpass + 1))
    else
      printf 'PASS  %s\n' "$rc_in"
      n_pass=$((n_pass + 1))
    fi
    return
  fi
  if [ -n "$rc_xfail" ]; then
    printf 'XFAIL %s (waiting for %s)\n' "$rc_in" "$rc_xfail"
    n_xfail=$((n_xfail + 1))
    return
  fi
  if [ "$rc_status" != 0 ]; then
    printf 'FAIL  %s (cli exited %s)\n' "$rc_in" "$rc_status"
    head -n 3 "$tmp" | sed 's/^/      /'
  else
    printf 'FAIL  %s\n' "$rc_in"
    if command -v diff >/dev/null 2>&1; then
      diff "$rc_exp" "$tmp" | head -n 12 | sed 's/^/      /'
    fi
  fi
  n_fail=$((n_fail + 1))
}

(cd "$root" && find examples tests/patches -name '*.orca' | LC_ALL=C sort) >"$list"
while IFS= read -r input; do
  run_case "$input"
done <"$list"

# Orphans: expected files and sidecars whose input is gone or misnamed.
(cd "$root" && find tests/expected -type f \
  \( -name '*.events' -o -name '*.args' -o -name '*.xfail' \) | LC_ALL=C sort) >"$list"
while IFS= read -r f; do
  base=${f%.*}
  case $base in
    tests/expected/examples/*) input="examples/${base#tests/expected/examples/}.orca" ;;
    tests/expected/patches/*) input="tests/patches/${base#tests/expected/patches/}.orca" ;;
    *) input= ;;
  esac
  if [ -z "$input" ] || [ ! -f "$root/$input" ]; then
    printf 'ORPHAN %s (no input %s)\n' "$f" "${input:-under examples/ or tests/patches/}"
    n_orphan=$((n_orphan + 1))
  fi
done <"$list"

if [ "$n_total" = 0 ]; then
  echo "no cases matched${filter:+ filter: $filter}" >&2
  exit 1
fi
if [ "$update" = 1 ]; then
  printf '%s cases: %s written, %s skipped (xfail), %s failed, %s orphaned\n' \
    "$n_total" "$n_updated" "$n_skipped" "$n_fail" "$n_orphan"
else
  printf '%s cases: %s pass, %s xfail, %s fail, %s xpass, %s orphaned\n' \
    "$n_total" "$n_pass" "$n_xfail" "$n_fail" "$n_xpass" "$n_orphan"
fi
[ "$n_fail" = 0 ] && [ "$n_xpass" = 0 ] && [ "$n_orphan" = 0 ]
