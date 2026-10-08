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
# fails the run unless a marker covers it. An object for which nm lists
# nothing fails too, so a check that sees nothing cannot pass.
#
# The canary, tests/nm/canary.c, holds one writable symbol of each class b,
# d, B and D, each with its address escaping, so -O2 keeps them. The same
# recipe must flag all four by name on every run, and the marker code must
# sort them with a built-in marker set: canary_static_* matches both
# statics, canary_extern_init its one symbol, canary_absent nothing (an
# XPASS), and canary_extern_zero, which no marker covers, is unmarked. If
# any of that fails, the check is blind and the run fails.
#
# tests/xfail/nm-check lists the expected writable symbols, one marker per
# line, as `<file> <symbol|glob> <item>`; # starts a comment. The pattern is
# a symbol name, or a literal prefix of at least 3 characters followed by one
# trailing * (scale_*); the item is the one spec item the fix waits for,
# matching ^(B[1-8]|I[1-6])$ (B1, I3). Each
# marker line reports on its own: XFAIL, naming its item and every writable
# symbol it matched (class and name), or XPASS when it matches none, which
# fails the run, so the series that removes a symbol deletes its line in the
# same commit. A writable symbol that no marker of its file matches fails the
# run as an unmarked symbol. A marker never hides an empty object or a failed
# compile. A marker naming a file outside CORE, a malformed line, any other
# pattern or a duplicate (file, pattern) fails the run.
#
# Exit status: 0 when the canary is flagged and every result is PASS or
# XFAIL, 1 otherwise, 2 on usage or on a system other than Linux.
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

# Markers, validated, as "<file> <pattern> <item>" lines in $tmp/markers.
: >"$tmp/markers"
if [ -f "$root/$marker_rel" ]; then
  if ! awk -v core="$core" -v name="$marker_rel" -v out="$tmp/markers" '
    function bad(why) { printf "FAIL  %s:%d: %s\n", name, FNR, why; errors++ }
    BEGIN { n = split(core, c, " "); for (i = 1; i <= n; i++) incore[c[i]] = 1 }
    {
      sub(/#.*/, "")
      if (NF == 0) next
      if (NF != 3) { bad("expected <file> <symbol|glob> <item>"); next }
      if (!($1 in incore)) { bad($1 " is not in CORE"); next }
      if ($2 !~ /^[A-Za-z0-9_.]+$/ &&
          $2 !~ /^[A-Za-z0-9_.][A-Za-z0-9_.][A-Za-z0-9_.]+\*$/) {
        bad("bad pattern " $2 " (expected a symbol name, or a prefix of at" \
          " least 3 characters and one trailing *)"); next
      }
      if ($3 !~ /^(B[1-8]|I[1-6])$/) { bad("bad item id " $3); next }
      if (($1 " " $2) in seen) { bad("duplicate marker for " $1 " " $2); next }
      seen[$1 " " $2] = 1
      print $1, $2, $3 > out
    }
    END { exit errors > 0 }
  ' "$root/$marker_rel"; then
    n_fail=$((n_fail + 1))
  fi
fi

# glob_match <symbol> <pattern>: whether the shell glob matches the symbol.
# set -f turns off pathname expansion only; case still matches patterns.
glob_match() {
  # shellcheck disable=SC2254  # the pattern is a glob on purpose
  case $1 in
    $2) return 0 ;;
  esac
  return 1
}

# marked <symbol>: whether a marker in $tmp/file_markers matches it.
marked() {
  while read -r mk_pattern _; do
    if glob_match "$1" "$mk_pattern"; then
      return 0
    fi
  done <"$tmp/file_markers"
  return 1
}

# matched_by <pattern>: prints the writable symbols the pattern matches, as
# "<class> <name>, ..." in nm order; nothing when it matches none.
matched_by() {
  mb_list=
  while read -r mb_class mb_name; do
    if glob_match "$mb_name" "$1"; then
      mb_list="$mb_list${mb_list:+, }$mb_class $mb_name"
    fi
  done <"$tmp/writable"
  printf '%s' "$mb_list"
}

# names_of <matched_by output>: the same list without the classes.
names_of() {
  printf '%s\n' "$1" | awk -F', ' '{
    for (i = 1; i <= NF; i++) {
      split($i, part, " ")
      printf "%s%s", (i > 1 ? ", " : ""), part[2]
    }
  }'
}

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
  # C collation, so the symbols list in the same order on every machine.
  if ! LC_ALL=C nm -P "$tmp/obj.o" >"$tmp/syms" 2>"$tmp/err"; then
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
why=
case $status in
  empty) why="nm lists no symbols for $canary_rel" ;;
  error) why="could not compile or read $canary_rel" ;;
  *)
    if [ -n "$missing" ] || [ "$status" != writable ]; then
      why="not flagged in $canary_rel:$missing"
    fi
    ;;
esac
# The marker path, through the same marked and matched_by as the CORE files.
if [ -z "$why" ]; then
  printf '%s B1\n' canary_static_* canary_extern_init canary_absent \
    >"$tmp/file_markers"
  unmarked=
  while read -r _ name; do
    if ! marked "$name"; then
      unmarked="$unmarked${unmarked:+ }$name"
    fi
  done <"$tmp/writable"
  statics=$(names_of "$(matched_by 'canary_static_*')")
  extern=$(names_of "$(matched_by canary_extern_init)")
  absent=$(matched_by canary_absent)
  if [ "$unmarked" != canary_extern_zero ]; then
    why="the markers left \"$unmarked\" unmarked, not canary_extern_zero"
  elif [ "$statics" != "canary_static_init, canary_static_zero" ]; then
    why="canary_static_* matched \"$statics\""
  elif [ "$extern" != canary_extern_init ]; then
    why="canary_extern_init matched \"$extern\""
  elif [ -n "$absent" ]; then
    why="canary_absent matched \"$absent\", not nothing"
  fi
fi
if [ -z "$why" ]; then
  printf 'PASS  canary (detected: %s; marker path: canary_extern_zero unmarked, canary_absent matches none)\n' \
    "$(awk '{ printf "%s%s %s", (NR > 1 ? ", " : ""), $1, $2 }' "$tmp/writable")"
  canary_ok=1
else
  printf 'FAIL  canary (the check is blind: %s)\n' "$why"
  head -n 3 "$tmp/err" | sed 's/^/      /'
fi

for file in $core; do
  n_files=$((n_files + 1))
  awk -v f="$file" '$1 == f { print $2, $3 }' "$tmp/markers" >"$tmp/file_markers"
  inspect "$file"
  case $status in
    error)
      printf 'FAIL  %s (could not compile or read it)\n' "$file"
      head -n 3 "$tmp/err" | sed 's/^/      /'
      n_fail=$((n_fail + 1))
      continue
      ;;
    empty)
      printf 'FAIL  %s (nm lists no symbols)\n' "$file"
      n_fail=$((n_fail + 1))
      continue
      ;;
  esac
  clean=1
  # Every writable symbol needs a marker of its file.
  while read -r class name; do
    if ! marked "$name"; then
      clean=0
      printf 'FAIL  %s %s %s (unmarked symbol)\n' "$file" "$class" "$name"
      n_fail=$((n_fail + 1))
    fi
  done <"$tmp/writable"
  # Each marker line reports on its own.
  while read -r pattern item; do
    clean=0
    matched=$(matched_by "$pattern")
    if [ -n "$matched" ]; then
      printf 'XFAIL %s %s (%s: %s)\n' "$file" "$pattern" "$item" "$matched"
      n_xfail=$((n_xfail + 1))
    else
      printf 'XPASS %s %s (%s: no writable symbol matches; remove the line from %s)\n' \
        "$file" "$pattern" "$item" "$marker_rel"
      n_xpass=$((n_xpass + 1))
    fi
  done <"$tmp/file_markers"
  if [ "$clean" = 1 ]; then
    printf 'PASS  %s\n' "$file"
    n_pass=$((n_pass + 1))
  fi
done

if [ "$canary_ok" = 1 ]; then
  canary_result="canary detected"
else
  canary_result="canary NOT detected"
fi
printf 'nm check: %s files, %s pass, %s xfail, %s fail, %s xpass; %s\n' \
  "$n_files" "$n_pass" "$n_xfail" "$n_fail" "$n_xpass" "$canary_result"
[ "$canary_ok" = 1 ] && [ "$n_fail" = 0 ] && [ "$n_xpass" = 0 ]
