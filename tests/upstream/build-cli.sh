#!/bin/sh
# Builds upstream Orca-c's cli at 9df9786, with bOrca's --events and --seed,
# into build/upstream/cli (architecture spine AD-21). Its output is what the
# goldens under tests/expected/examples/upstream/ hold, and what I1 compares
# `cli --dialect upstream` with.
#
#   tests/upstream/build-cli.sh
#
# 1. Gets Orca-c 9df9786e2ad3c01955cdf4cdd5ae1fffad8fa5cc with `git archive`:
#    from this repository when it has the commit (a fetched `upstream` remote
#    for hundredrabbits/Orca-c), otherwise from a depth-1 fetch of that commit
#    by URL from https://github.com/hundredrabbits/Orca-c.git into
#    build/upstream/orca-c.git, which later runs reuse.
# 2. Extracts it to build/upstream/orca-c/, replacing any earlier tree.
# 3. Copies this repository's events_print.c and events_print.h into it, so
#    both cli builds print events through one file.
# 4. Applies tests/upstream/orca-c-cli.patch (`cli --events`, `--seed`, and
#    events_print.c in Orca-c's tool) with `patch -p1`.
# 5. Builds Orca-c's release cli with Orca-c's own tool and copies it to
#    build/upstream/cli.
#
# A golden for examples/upstream/<dir>/<name>.orca is then
#
#   build/upstream/cli --events -t 96 --seed 0 examples/upstream/<dir>/<name>.orca \
#     >tests/expected/examples/upstream/<dir>/<name>.events
#
# Needs git, tar, patch and a C compiler. On the uConsole run it as
# `nice -n 19 taskset -c 0-2 tests/upstream/build-cli.sh`. Exits 0 on
# success, non-zero on the first failing step.
set -eu

sha=9df9786e2ad3c01955cdf4cdd5ae1fffad8fa5cc
url=https://github.com/hundredrabbits/Orca-c.git

if [ "$#" != 0 ]; then
  echo "usage: $0" >&2
  exit 2
fi

root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/build/upstream"
tree="$out/orca-c"
cache="$out/orca-c.git"
archive="$out/orca-c.tar"

has_commit() {
  git -C "$1" cat-file -e "$sha^{commit}" 2>/dev/null
}

mkdir -p "$out"
rm -rf "$tree" "$archive" "$out/cli"

if has_commit "$root"; then
  src=$root
else
  if ! has_commit "$cache"; then
    echo "Fetching Orca-c $sha from $url"
    rm -rf "$cache"
    git init -q --bare "$cache"
    git -C "$cache" fetch -q --depth 1 "$url" "$sha"
    has_commit "$cache"
  fi
  src=$cache
fi
echo "Orca-c $sha: git archive from $src"
# Through a file, not a pipe, so a failed archive stops the script.
git -C "$src" archive --format=tar -o "$archive" "$sha"
mkdir "$tree"
tar -x -f "$archive" -C "$tree"
rm "$archive"

cp "$root/events_print.c" "$root/events_print.h" "$tree/"
patch -p1 -N -d "$tree" -i "$root/tests/upstream/orca-c-cli.patch"
(cd "$tree" && ./tool build cli)
cp "$tree/build/cli" "$out/cli"
echo "Built $out/cli"
