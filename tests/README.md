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

## Unit tests and core checks

### Running the unit tests

`./tool build test` builds `build/unit_tests` (`build/debug/unit_tests` with
`-d`) from the CORE list plus every `tests/unit/*.c`, against libc only. The
runner prints `ok <name>` for each passing test and
`FAIL <name>: <file>:<line>: <expr>` for each failed check, then
`<n> tests, <m> failed`, and exits 0 only when no test failed. A failed check
does not stop the run, but a crash, such as an ASan abort, ends it with a
non-zero exit status and no summary line.

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d test
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build test
nice -n 19 taskset -c 0-2 build/unit_tests
```

The debug runner is built with ASan and UBSan. UBSan on its own prints a
report and carries on, so the runner defines `__ubsan_default_options()` to
return `halt_on_error=1:print_stacktrace=1`, and a report fails the run even
without `UBSAN_OPTIONS`. CI and the commands above still set
`UBSAN_OPTIONS=halt_on_error=1` explicitly; `UBSAN_OPTIONS` overrides the
runner's defaults.

`tool` defines the CORE list once (today `gbuffer.c vmio.c sim.c`; the
extracted core modules and `tick.c` join it later). `cli`, `orca` and `test`
link all of it, and `./tool sources <core|cli|orca|test>` prints a list, one
file per line, so the checks below and the CI `armhf` job read the same lists.

Adapter `FEAT_` flags apply to `test` as they do to `orca`: `--portmidi`
builds the PortMidi adapter tests (`--alsa` arrives with P0.7), and
`--no-mouse`, which has no adapter, is ignored. A build that sets a flag but
registers no test for it fails at run time with a message naming the flag.
The PortMidi build needs `libportmidi-dev`, so it runs in CI only, not on
the uConsole. `-DUNIT_TESTS_CANARY` (through `CFLAGS_EXTRA`) adds
`unit_check_canary`, a test whose check is false; CI uses it to prove the
runner reports a failure and exits 1. `./tool build test` stops with an
error if `tests/unit/main.c`, the runner, is missing.

### Adding a unit test

1. Write `void test_<name>(void)` in a top-level `tests/unit/*.c` file that
   includes `tests.h`; `tool` picks up every such file, but not files in
   subdirectories. Include core headers by relative path (`"../../sim.h"`),
   so no build needs `-I.`.
2. Check with `CHECK(expr)`, which records the failure and lets the test
   continue. Never use `assert`: release builds define `NDEBUG`, which
   removes it.
3. In `tests/unit/tests.h`, add `X(<name>)` to `CORE_TESTS`, or to the full
   list for its adapter flag (`PORTMIDI_TESTS_ALL`). `tests.h` generates
   every test's prototype from these lists, outside any `#ifdef`, and makes
   `-Wmissing-prototypes` an error, so a `test_<name>` that no list names
   fails to build instead of never running. Make helper functions `static`.
4. An adapter test sits inside `#ifdef FEAT_<flag>` in its file and may
   include that backend's header; a core test includes only core headers and
   libc. The declarations in `tests.h` keep a file whose flag is off from
   being an empty translation unit, which `-Wpedantic` rejects.
5. Free whatever the test allocates: the debug runner also runs
   LeakSanitizer.

### Core checks

Both scripts check every file in `./tool sources core` (an empty list is an
error), call `gcc` by name, run from any directory and exit 0 only when their
canary is detected and every result is PASS or XFAIL.

- `tests/check-includes.sh` (spine AD-2) strips comments with
  `gcc -fpreprocessed -dD -E -P`, then reports each `#include` in the CORE
  file, or in a core header it includes, that is not on the allow-list:
  `base.h`, `vmio.h`, `gbuffer.h`, the other core headers AD-2 names, and
  `<assert.h>`, `<limits.h>`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`,
  `<stdlib.h>` and `<string.h>`. It also reports `rand` as a whole word in
  code. `base.h` is shared foundation, allowed whole and not scanned.
  Comments do not count. Known edges: `rand` in a string literal or an
  `#if 0` block is reported, and `srand` is not; `base.h` includes
  `<unistd.h>`, so `read()`, `write()`, `usleep()` and the rest of it reach
  every core file and pass the check. The canary `tests/includes/canary.c`
  includes `<stdio.h>`; it is scanned first, the same way as a CORE file,
  and must be reported, or the check is blind and the run fails.
- `tests/check-nm.sh` (spine AD-1) compiles each CORE file on its own with
  `gcc -c -std=c99 -O2 -DNDEBUG -g0 -fno-pie -no-pie -fno-lto` and fails it
  when `nm -P` lists a writable symbol (class `B b C D d G g S s V v`) or no
  symbol at all. The canary `tests/nm/canary.c` holds one writable symbol
  of each class `b`, `d`, `B` and `D`, each with its address escaping; the
  check must flag all four by name on every run, or it is blind and the run
  fails. The script is Linux/ELF only: elsewhere it exits 2.

Today they print:

```text
$ tests/check-includes.sh
PASS  canary (detected: <stdio.h>)
PASS  gbuffer.c
PASS  vmio.c
XFAIL sim.c rand (B1)
include check: 3 files, 2 pass, 1 xfail, 0 fail, 0 xpass; canary detected
$ tests/check-nm.sh
PASS  canary (detected: D canary_extern_init, B canary_extern_zero, d canary_static_init, b canary_static_zero)
PASS  gbuffer.c
PASS  vmio.c
XFAIL sim.c (B1 B3)
nm check: 3 files, 2 pass, 1 xfail, 0 fail, 0 xpass; canary detected
```

In the summaries, `pass` counts clean files, and `xfail`, `fail` and `xpass`
count results, so one file can add several.

### Check markers

`tests/xfail/<check>` lists the violations a check expects, one per line;
`#` starts a comment.

| Marker file | Line format | Today |
| --- | --- | --- |
| `tests/xfail/include-check` | `<file> <pattern> <item...>`, where the pattern is `rand`, `<name.h>` or `"name.h"` | `sim.c rand B1` |
| `tests/xfail/nm-check` | `<file> <item...>` | `sim.c B1 B3` |

- The items are the spec items the fix waits for, matching
  `[A-Z][0-9]+(\.[0-9]+)?` (`B1`, `I6.2`). A marked violation reports XFAIL
  and the run stays green.
- A marker whose violation is gone reports XPASS and fails the run: the
  series that turns a check green deletes the marker line in the same
  commit.
- A marker that names a file outside CORE, a malformed line or a duplicate
  fails the run. An nm marker never hides an object with no symbols or a
  failed compile.

## CI

`.github/workflows/ci.yml` runs on pushes and pull requests to
`spec/borca-fixes`; start a manual run with
`gh workflow run ci.yml --ref spec/borca-fixes`.

- **build** (pinned `ubuntu-24.04` and `ubuntu-24.04-arm`, never `-latest`):
  first checks that `CFLAGS_EXTRA` reaches the compiler, then builds the
  debug and release `cli` with `CFLAGS_EXTRA=-Werror` and runs the golden
  suite on both. It then runs `tests/check-includes.sh` and
  `tests/check-nm.sh`, which need no build, builds and runs the debug unit
  tests without and with `--portmidi` (ASan, UBSan with `halt_on_error=1`;
  the `--portmidi` run fails unless it prints
  `ok portmidi_error_text_and_filters`), and builds the debug `orca`, the
  debug `orca` with PortMidi (`make debug`), the release `orca` without
  mouse support and the release `orca` with PortMidi, all with `-Werror`.
- **unsigned-char** (`ubuntu-24.04`): runs the golden suite on a release
  `cli`, and the release unit tests, both built with `-funsigned-char`;
  checks that a release runner built with `-DUNIT_TESTS_CANARY` exits 1 and
  prints `FAIL unit_check_canary:`; and runs
  `shellcheck -s sh tool tests/run.sh tests/check-includes.sh tests/check-nm.sh`.
- **armhf** (`ubuntu-24.04`, optional): only cross-compiles, with
  `arm-linux-gnueabihf-gcc`, each file in the union of `./tool sources cli`
  and `./tool sources test`; a red result does not fail the run.

`tool` adds `CFLAGS_EXTRA` after its own compiler flags, split on spaces. To
reproduce a red `build` or `unsigned-char` step on the uConsole:

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d cli
nice -n 19 taskset -c 0-2 tests/run.sh build/debug/cli
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build cli
nice -n 19 taskset -c 0-2 tests/run.sh build/cli
nice -n 19 taskset -c 0-2 tests/check-includes.sh
nice -n 19 taskset -c 0-2 tests/check-nm.sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d test
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build test
nice -n 19 taskset -c 0-2 build/unit_tests
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d orca
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build --no-mouse orca
```

On aarch64 plain `char` is already unsigned, so the release `cli` and
release unit-test runs above cover the `unsigned-char` job. The x86_64
`build` leg uses signed `char`; reproduce a failure that only it shows with:

```sh
CFLAGS_EXTRA="-Werror -fsigned-char" nice -n 19 taskset -c 0-2 ./tool build cli
nice -n 19 taskset -c 0-2 tests/run.sh build/cli
CFLAGS_EXTRA="-Werror -fsigned-char" nice -n 19 taskset -c 0-2 ./tool build -d test
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests
```

Some steps cannot be reproduced on the uConsole as it stands: the PortMidi
builds, including `-d --portmidi test`, need `libportmidi-dev` (installing
it needs Ian's approval), and the `armhf` job needs the armhf cross
compiler. `tests/check-nm.sh` results depend on the architecture, so an
x86_64 result from the `build` leg may differ from the uConsole's aarch64
one. The runners' gcc can warn where the uConsole's gcc 12.2 does not; each
job prints its compiler version in its "Toolchain versions" step.
