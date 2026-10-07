# Tests

## Golden tests

`tests/run.sh <path-to-cli> [name-filter]` runs every patch under `examples/`
and `tests/patches/` through `cli --events` and compares the output byte for
byte with the expected file. Run it on both builds:

```sh
./tool build cli      && tests/run.sh build/cli
./tool build -d cli   && tests/run.sh build/debug/cli    # the debug build
```

On the uConsole prefix both with `nice -n 19 taskset -c 0-2`, so the live
engine on core 3 is left alone. The optional second argument runs only the
cases whose path contains it (`tests/run.sh build/cli midichord`).

Default flags are `-t 64 --seed 0 --dialect borca`. The line formats are the
ones `cli --events` prints (`t<tick> NOTE ch<c> note<n> vel<v> len<l>
mono<m>`, `CC`, `CCI`, `PB`, `OSC`, `UDP`, then `t<tick> GRID` and the grid
rows); see `events_print.c` and `cli -h`. The script exits 0 only when every
case is PASS or XFAIL and no expected file is orphaned.

### Layout

| Input | Expected output | Sidecars (same directory) |
| --- | --- | --- |
| `examples/<dir>/<name>.orca` (referenced in place, never copied) | `tests/expected/examples/<dir>/<name>.events` | `<name>.args`, `<name>.xfail` |
| `tests/patches/<name>.orca` (repro patches) | `tests/expected/patches/<name>.events` | `<name>.args`, `<name>.xfail` |

- `<name>.args` holds extra flags appended after the defaults; a later flag
  wins, so `-t 1` in a sidecar overrides the default `-t 64`.
- `<name>.xfail` holds the ids of the spec items whose fixes the case waits
  for, space-separated (`B2`, or `B2 B5`). While a fix is missing the case
  reports XFAIL and the run stays green. Once the output matches, the case
  reports XPASS and the run fails: the commit that lands an item removes its
  id from the marker, and deletes the file when no id is left.
- A marker never hides a crash. A sanitizer report in the output, a signal or
  the 60 s per-case timeout fails the run whatever the marker says, so the
  debug build must stay clean on every case. A plain non-zero exit (such as a
  rejected flag) counts as a mismatch and may be marked.
- An expected file or sidecar whose input no longer exists under `examples/`
  or `tests/patches/` is reported as ORPHAN and fails the run, so renamed or
  removed patches cannot leave stale goldens behind.

### Updating expected files

`tests/run.sh --update <cli>` rewrites every expected file from the current
output, except cases that carry a `.xfail` marker, whose expected files
describe the fixed behaviour and are written by hand. Use it when a change is
meant to alter output; the diff of `tests/expected/` is then part of the
review.

### Adding a repro patch

1. Put the patch in `tests/patches/<name>.orca`, and `-t <n>` in
   `tests/expected/patches/<name>.args` when 64 ticks are more than it needs.
2. Write `tests/expected/patches/<name>.events` by hand with the fixed
   behaviour (start from the current output and edit the lines the fix
   changes).
3. Put the item id in `tests/expected/patches/<name>.xfail`.
4. Run the suite: the case must report XFAIL, not FAIL.

### Goldens that record current behaviour on purpose

`r_single` and `r_shared` are replaced, not marked expected-fail, when B6
lands: B6's exact sequence depends on the PRNG it picks, so its fixed output
cannot be written ahead of time; B6 replaces these files and proves the
no-repeat and permutation properties in a unit test. Until then lowercase `r`
shuffles with glibc's unseeded `rand()`, so these two goldens hold the glibc
stream: they pass on Linux and fail on macOS or musl, and `--seed` does not
affect them.

`j_banged_under_j` and `y_banged_right_of_y` were captured before the
upstream "Allow wires to grow" cherry-pick (P0.1) and replaced by it; together
with `j_reads_locked_J`, added with that pick, they now pin the grown-wire
behaviour.

The `&` state-overflow fixture is a sanitizer check, not a golden: its output
differs between builds until B1 lands, so B1 adds it under the unit-test
target.

## CI

`.github/workflows/ci.yml` runs on pushes and pull requests to
`spec/borca-fixes`; start a manual run with
`gh workflow run ci.yml --ref spec/borca-fixes`.

- **build** (pinned `ubuntu-24.04` and `ubuntu-24.04-arm`, never `-latest`):
  first checks that `CFLAGS_EXTRA` reaches the compiler, then builds the
  debug and release `cli` with `CFLAGS_EXTRA=-Werror` and runs this suite on
  both, then builds the debug `orca`, the debug `orca` with PortMidi (`make
  debug`), the release `orca` without mouse support and the release `orca`
  with PortMidi, all with `-Werror`.
- **unsigned-char** (`ubuntu-24.04`): runs the suite on a release `cli`
  built with `-funsigned-char`.
- **armhf** (`ubuntu-24.04`, optional): only cross-compiles the `cli`
  sources with `arm-linux-gnueabihf-gcc`; a red result does not fail the run.

`tool` adds `CFLAGS_EXTRA` after its own compiler flags, split on spaces. To
reproduce a red `build` or `unsigned-char` step on the uConsole:

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d cli
nice -n 19 taskset -c 0-2 tests/run.sh build/debug/cli
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build cli
nice -n 19 taskset -c 0-2 tests/run.sh build/cli
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d orca
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build --no-mouse orca
```

On aarch64 plain `char` is already unsigned, so the release run above
covers the `unsigned-char` job. The x86_64 `build` leg uses signed `char`;
reproduce a failure that only it shows with:

```sh
CFLAGS_EXTRA="-Werror -fsigned-char" nice -n 19 taskset -c 0-2 ./tool build cli
nice -n 19 taskset -c 0-2 tests/run.sh build/cli
```

Two kinds of step cannot be reproduced on the uConsole as it stands: the
PortMidi builds need `libportmidi-dev` (installing it needs Ian's
approval), and the `armhf` job needs the armhf cross compiler. The runners'
gcc can warn where the uConsole's gcc 12.2 does not; each job prints its
compiler version in its "Toolchain versions" step.
