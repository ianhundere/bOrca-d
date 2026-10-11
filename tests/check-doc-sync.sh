#!/bin/sh
# Doc-sync check (architecture spine AD-19, AD-21): README.md's scale and
# chord tables must be exactly what readme-gen prints from music.h.
#
#   tests/check-doc-sync.sh <path-to-readme-gen> [README]
#
# README defaults to the repository's README.md. It must hold exactly one
# <!-- tables:begin --> line and one <!-- tables:end --> line, each alone on
# its line, begin first. The lines strictly between them are compared with
# readme-gen's output by `diff -u`, which is printed on a mismatch. To fix a
# mismatch, replace those lines with the output of readme-gen.
#
# Exit status: 0 when the two are identical, 1 when they differ or the README
# or readme-gen is unusable (missing or misplaced markers, a failed or empty
# readme-gen run), 2 on usage.
set -u

begin_marker='<!-- tables:begin -->'
end_marker='<!-- tables:end -->'

root=$(cd "$(dirname "$0")/.." && pwd)
gen=${1-}
readme=${2-$root/README.md}
if [ -z "$gen" ] || [ ! -x "$gen" ] || [ "$#" -gt 2 ]; then
  echo "usage: $0 <path-to-readme-gen> [README]" >&2
  exit 2
fi
case $gen in
  /*) ;;
  *) gen="$PWD/$gen" ;;
esac
if [ ! -r "$readme" ]; then
  echo "FAIL  doc-sync: cannot read $readme"
  exit 1
fi

# marker_line <marker>: prints the line number of the one line that is
# exactly the marker, or a reason on stderr and returns 1. A line that only
# contains the marker counts against it, so a doubled or mangled marker fails.
marker_line() {
  ml_any=$(grep -c -F -e "$1" "$readme")
  ml_exact=$(grep -c -x -F -e "$1" "$readme")
  if [ "$ml_any" != 1 ] || [ "$ml_exact" != 1 ]; then
    printf '%s needs exactly one %s line, alone on it; found %s line(s) holding it, %s alone\n' \
      "$readme" "$1" "$ml_any" "$ml_exact" >&2
    return 1
  fi
  grep -n -x -F -e "$1" "$readme" | cut -d: -f1
}

if ! begin_line=$(marker_line "$begin_marker") ||
  ! end_line=$(marker_line "$end_marker"); then
  echo "FAIL  doc-sync: bad markers"
  exit 1
fi
if [ "$begin_line" -ge "$end_line" ]; then
  printf 'FAIL  doc-sync: %s (line %s) must come before %s (line %s)\n' \
    "$begin_marker" "$begin_line" "$end_marker" "$end_line"
  exit 1
fi

tmpd=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpd"' EXIT
trap 'exit 130' INT TERM
want="$tmpd/README.md-tables"
got="$tmpd/readme-gen-output"

awk -v b="$begin_line" -v e="$end_line" 'NR > b && NR < e' "$readme" >"$want"
if ! "$gen" >"$got"; then
  echo "FAIL  doc-sync: $gen exited non-zero"
  exit 1
fi
if [ ! -s "$got" ]; then
  echo "FAIL  doc-sync: $gen printed nothing"
  exit 1
fi

status=0
diff -u "$want" "$got" || status=$?
case $status in
  0)
    echo "PASS  doc-sync: $readme lines $((begin_line + 1))-$((end_line - 1)) match $gen"
    ;;
  1)
    echo "FAIL  doc-sync: $readme's tables differ from $gen's output (- README, + readme-gen); replace the lines between the markers with that output"
    exit 1
    ;;
  *)
    echo "FAIL  doc-sync: diff exited $status"
    exit 1
    ;;
esac
