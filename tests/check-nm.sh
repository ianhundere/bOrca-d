#!/bin/sh
# No-writable-globals check for the CORE files (architecture spine AD-1).
# Linux/ELF only: it relies on GNU-style nm symbol classes and gcc's
# -no-pie. See tests/README.md.
#
#   tests/check-nm.sh
#
# Compiles each file that `./tool sources core` lists on its own, with
#   gcc -c -std=c99 -O2 -DNDEBUG -g0 -fno-pie -no-pie -fno-lto
# (never LTO or strip, which hide symbols, and never PIE, under which const
# pointer tables show as writable), and reads each symbol's class, field 2 of
# `nm -P`. A symbol of class B b C D d G g S s V v is writable data or bss and
# fails the file. An object for which nm lists nothing fails too, so a check
# that sees nothing cannot pass.
#
# The canary, tests/nm/canary.c, holds one writable symbol of each class b,
# d, B and D, each with its address escaping, so -O2 keeps them. The same
# recipe must flag all four by name on every run; if it misses one, the check
# is blind and the run fails.
#
# tests/xfail/nm-check lists the files expected to fail, one per line, as
# `<file> <item...>`; # starts a comment. The items are the spec items the
# fix waits for (B1, I6.2). A marked file reports XFAIL and does not fail the
# run. A marked file that passes reports XPASS and fails the run, so the
# series that makes it pass deletes the line in the same commit. A marker
# never hides an empty object or a failed compile. A marker naming a file
# outside CORE, a malformed line or a duplicate fails the run.
#
# Exit status: 0 when the canary is flagged and every file is PASS or XFAIL,
# 1 otherwise, 2 on usage or on a system other than Linux.
set -uf

root=$(cd "$(dirname "$0")/.." && pwd)
if [ "$#" != 0 ]; then
  echo "usage: $0" >&2
  exit 2
fi
if [ "$(uname -s)" != Linux ]; then
  echo "error: $0 is Linux/ELF only; nm's symbol classes differ elsewhere" >&2
  exit 2
fi
marker_rel=tests/xfail/nm-check
canary_rel=tests/nm/canary.c
# The canary's symbols, one per writable class (b d B D); each must be flagged.
canary_symbols='canary_static_zero canary_static_init canary_extern_zero canary_extern_init'
nm_flags='-c -std=c99 -O2 -DNDEBUG -g0 -fno-pie -no-pie -fno-lto'

for tool_name in gcc nm; do
  if ! command -v "$tool_name" >/dev/null 2>&1; then
    echo "error: $tool_name not found" >&2
    exit 1
  fi
done
if ! core=$(cd "$root" && ./tool sources core); then
  echo "error: ./tool sources core failed" >&2
  exit 1
fi
if [ -z "$core" ]; then
  echo "error: ./tool sources core listed no files" >&2
  exit 1
fi
# One line, space-separated (no core file name contains a space).
# shellcheck disable=SC2086  # split the one-per-line list
core=$(printf '%s ' $core)

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
trap 'exit 130' INT TERM

n_files=0 n_pass=0 n_fail=0 n_xfail=0 n_xpass=0

# Markers, validated, as "<file> <items>" lines in $tmp/markers.
: >"$tmp/markers"
if [ -f "$root/$marker_rel" ]; then
  if ! awk -v core="$core" -v name="$marker_rel" -v out="$tmp/markers" '
    function bad(why) { printf "FAIL  %s:%d: %s\n", name, FNR, why; errors++ }
    BEGIN { n = split(core, c, " "); for (i = 1; i <= n; i++) incore[c[i]] = 1 }
    {
      sub(/#.*/, "")
      if (NF == 0) next
      if (NF < 2) { bad("expected <file> <item...>"); next }
      if (!($1 in incore)) { bad($1 " is not in CORE"); next }
      items = ""
      for (i = 2; i <= NF; i++) {
        if ($i !~ /^[A-Z][0-9]+(\.[0-9]+)?$/) { bad("bad item id " $i); next }
        items = items (i > 2 ? " " : "") $i
      }
      if ($1 in seen) { bad("duplicate marker for " $1); next }
      seen[$1] = 1
      print $1, items > out
    }
    END { exit errors > 0 }
  ' "$root/$marker_rel"; then
    n_fail=$((n_fail + 1))
  fi
fi

# inspect <source relative to root>: compiles it with the recipe and sets
# status to error, empty, writable or clean; $tmp/writable then holds the
# writable symbols as "<class> <name>" lines.
inspect() {
  rm -f "$tmp/obj.o"
  : >"$tmp/writable"
  # shellcheck disable=SC2086  # $nm_flags is a word list
  if ! gcc $nm_flags "$root/$1" -o "$tmp/obj.o" 2>"$tmp/err"; then
    status=error
    return
  fi
  if ! nm -P "$tmp/obj.o" >"$tmp/syms" 2>"$tmp/err"; then
    status=error
    return
  fi
  if ! [ -s "$tmp/syms" ]; then
    status=empty
    return
  fi
  awk '$2 ~ /^[BbCDdGgSsVv]$/ { print $2, $1 }' "$tmp/syms" >"$tmp/writable"
  if [ -s "$tmp/writable" ]; then
    status=writable
  else
    status=clean
  fi
}

canary_ok=0
inspect "$canary_rel"
missing=
for sym in $canary_symbols; do
  if ! awk -v s="$sym" '$2 == s { found = 1 } END { exit !found }' "$tmp/writable"; then
    missing="$missing $sym"
  fi
done
if [ "$status" = writable ] && [ -z "$missing" ]; then
  printf 'PASS  canary (detected: %s)\n' \
    "$(awk '{ printf "%s%s %s", (NR > 1 ? ", " : ""), $1, $2 }' "$tmp/writable")"
  canary_ok=1
else
  case $status in
    empty) why="nm lists no symbols for $canary_rel" ;;
    error) why="could not compile or read $canary_rel" ;;
    *) why="not flagged in $canary_rel:$missing" ;;
  esac
  printf 'FAIL  canary (the check is blind: %s)\n' "$why"
  head -n 3 "$tmp/err" | sed 's/^/      /'
fi

for file in $core; do
  n_files=$((n_files + 1))
  items=$(awk -v f="$file" '$1 == f { $1 = ""; sub(/^ /, ""); print }' "$tmp/markers")
  inspect "$file"
  case $status in
    error)
      printf 'FAIL  %s (could not compile or read it)\n' "$file"
      head -n 3 "$tmp/err" | sed 's/^/      /'
      n_fail=$((n_fail + 1))
      ;;
    empty)
      printf 'FAIL  %s (nm lists no symbols)\n' "$file"
      n_fail=$((n_fail + 1))
      ;;
    writable)
      if [ -n "$items" ]; then
        printf 'XFAIL %s (%s)\n' "$file" "$items"
        n_xfail=$((n_xfail + 1))
      else
        printf 'FAIL  %s (writable symbols: %s)\n' "$file" \
          "$(wc -l <"$tmp/writable" | tr -d ' ')"
        head -n 10 "$tmp/writable" | sed 's/^/      /'
        n_fail=$((n_fail + 1))
      fi
      ;;
    clean)
      if [ -n "$items" ]; then
        printf 'XPASS %s (marked xfail for %s: remove the line from %s)\n' \
          "$file" "$items" "$marker_rel"
        n_xpass=$((n_xpass + 1))
      else
        printf 'PASS  %s\n' "$file"
        n_pass=$((n_pass + 1))
      fi
      ;;
  esac
done

if [ "$canary_ok" = 1 ]; then
  canary_result="canary detected"
else
  canary_result="canary NOT detected"
fi
printf 'nm check: %s files, %s pass, %s xfail, %s fail, %s xpass; %s\n' \
  "$n_files" "$n_pass" "$n_xfail" "$n_fail" "$n_xpass" "$canary_result"
[ "$canary_ok" = 1 ] && [ "$n_fail" = 0 ] && [ "$n_xpass" = 0 ]
