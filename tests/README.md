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
- `<name>.xfail` names the one spec item whose fix the case waits for:
  exactly one id matching `^(B[1-8]|I[1-6])$` (`B2`). A repro that waits for
  two items is two cases, one per item, so each flips in the commit that
  lands its item. While the fix is missing the case reports XFAIL and the
  run stays green. Once the output matches, the case reports XPASS and the
  run fails: the commit that lands the item deletes the marker.
- A marker that holds no id, anything but one such id (`TODO`, `B9`, `b2`)
  or two ids fails the case as `FAIL <case> (bad marker: …)`, in both modes;
  `--update` never rewrites that case's expected file, and the other cases
  still run.
- A marker never hides a crash. A sanitizer report in the output, a signal or
  the 60 s per-case timeout fails the run whatever the marker says, so the
  debug build must stay clean on every case. A plain non-zero exit (such as a
  rejected flag) counts as a mismatch and may be marked.
- An expected file or sidecar whose input no longer exists under `examples/`
  or `tests/patches/` is reported as ORPHAN and fails the run, so renamed or
  removed patches cannot leave stale goldens behind.

### Updating expected files

`tests/run.sh --update <cli>` rewrites every expected file from the current
output, except cases that carry a `.xfail` marker, valid or bad, whose
expected files describe the fixed behaviour and are written by hand. Use it
when a change is meant to alter output; the diff of `tests/expected/` is
then part of the review.

### Adding a repro patch

1. Put the patch in `tests/patches/<name>.orca`, and `-t <n>` in
   `tests/expected/patches/<name>.args` when 64 ticks are more than it needs.
2. Write `tests/expected/patches/<name>.events` by hand with the fixed
   behaviour (start from the current output and edit the lines the fix
   changes).
3. Put the one item id in `tests/expected/patches/<name>.xfail`. A repro
   that two items change is split so that each case waits for one.
4. Run the suite: the case must report XFAIL, not FAIL.

The marked repros today are `scale_inv` (`$3CA2`, `$3CA0`) and
`midichord_inv` (`=13CA.1`), which wait for B2, and `midichord_vel`
(`=13Caf1`, `=13C0f1`, `=13Ca01`), which waits for B5. `midichord_inv` uses
velocity `.` so that B5 cannot change it; CAP-7's literal `=13CAf1`, which
both items change, belongs to B2's story. When these fixed goldens were
written, a throwaway script derived them from the current output and
asserted each line it edited; the script is not committed.

### Goldens that record current behaviour on purpose

`r_single` and `r_shared` record lowercase `r` as it is today. Each `r`
keeps its own bag in the op-state store and shuffles it with the PCG32 in
`prng.h`, seeded from `--seed`, its row and its column, so the goldens hold
the same stream on every platform, and `r_shared`'s `0r3` emits exactly
`r_single`'s sequence. They are replaced, not marked expected-fail: B1
replaced the glibc `rand()` stream they held before, when it removed
`rand()` from `sim.c` (spine AD-1), and B6 replaces them again when it fixes
`r`. B6 proves the no-repeat and permutation properties in a unit test.

Eight characterization goldens pin, unmarked, what the operators that
Phase 1 touches produce today, so a refactor that changes their output
fails a case. Three are replaced, not marked, by the Phase 2 item that
redefines their operator: `bouncer_shapes` by I3, and `arp_patterns` and the
`;` rows of `seeded_random` by I4.

| Case | Pins | `.args` | Replaced by |
| --- | --- | --- | --- |
| `bouncer_shapes` | `&` shapes 0–7 at rate 5 over `0`–`z`, an end below the start, a partial range, rate `.`, shape `z`, a `D` bang that resets the phase every 7 ticks, and a rate and a shape that a `C` clock changes, each change resetting the phase | none | I3 |
| `arp_patterns` | `;` ranges 1–4 with every non-random pattern, ranges `0` and `z`, a bang every 3 ticks, and a clocked pattern change that restarts the step | none | I4 |
| `seeded_random` | `R` with a lowercase, an uppercase and a `0` max, and `;` pattern `c`, under a non-zero seed | `--seed 7` | I4 (its `;` rows) |
| `cc_instant` | `!` at rate `.` on CC 1 and CC 74 with values `0`, `g`, `v` and `w` (clamped to 127), sent again on every tick; a channel above 15 that sends nothing, a hundreds digit (CC 100), and controller 130 clamped to 127 | `-t 2` | none |
| `cc_rates` | the VM's classification of `!` at rates `0` and `z`: still `CCI`, with the rate index (0 and 35) (B3, spine AD-11). `cli` runs no CC engine, so the unit test `tick_cci_same_tick` checks that both go out in the tick that bangs them | `-t 2` | none |
| `pitch_bend` | `?` with MSB and LSB at `0`, `z` and between, and a channel above 15 that sends nothing | `-t 2` | none |
| `scale_selectors` | `$` with every digit and lowercase selector at degrees 0, 2, 5 and 9 (with `$3Ca2` and `$3C02`), other roots, no octave, input octave `a` clamped to 9, and a result above octave 9 that writes nothing | `-t 1` | none |
| `chord_notes` | `=` with every digit and lowercase chord at velocity `.` and at `z`, an octave-9 chord that drops the notes above 127, input octave `a` clamped to 9, a channel above 15, and `:13Cf1` | `-t 1` | none |

Their patches avoid what Phase 1 changes on purpose: no lowercase `r`
operator (B1 and B6 change it; `r` appears only as a `$` or `=` selector,
which those operators lock), no uppercase `$` or `=` selector (B2 changes
them), and no `=` velocity other than `.` or `z`, which give 127 before and
after B5. Every `&` and `;` cell keeps y × width + x below 4096: before B1,
`&` indexed its state with no bounds check and `;` returned early from cell
4096 on. Each case was captured with `--update`. When the cases were
written, a throwaway model of the operators' source checked each cell; it
is not committed.

The `R` and `;` pattern `c` goldens assume a 64-bit `Usz`: their hashes
differ where `Usz` is 32-bit, as on armhf, whose CI job only compiles.

`j_banged_under_j` and `y_banged_right_of_y` were captured before the
upstream "Allow wires to grow" cherry-pick (P0.1) and replaced by it; together
with `j_reads_locked_J`, added with that pick, they now pin the grown-wire
behaviour.

The `&` state-overflow fixture is a unit test, not a golden
(`sim_bouncer_overflow_fixture` in `tests/unit/test_sim_state.c`): before
B1 its output differed between builds, because the overflow read whatever
lay past the array. It builds the 300×80 grid in memory, runs 256 ticks
under the debug runner's ASan and UBSan, and checks that every `&` outputs
what a lone in-range one does.

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

`tool` defines the CORE list once (today `gbuffer.c vmio.c sim.c
opstate.c ccout.c tick.c`; the other extracted core modules join it later).
`tick.c` is shell code, the tick body and the sustained-note list, held to
the core rules (spine AD-14), so the unit tests can drive it through a
recording sink.
`cli`, `orca` and `test` link all of it, and
`./tool sources <core|cli|orca|test>` prints a list, one file per line, so
the checks below and the CI `armhf` job read the same lists.

Adapter `FEAT_` flags apply to `test` as they do to `orca`: `--portmidi`
builds the PortMidi adapter tests, `--alsa` builds the ALSA adapter tests,
and `--no-mouse`, which has no adapter, is ignored. A build that sets a flag
but registers no test for it fails at run time with a message naming the
flag. The adapter lists in `tests/unit/tests.h` today:

| Flag | List | Tests |
| --- | --- | --- |
| `--portmidi` (`FEAT_PORTMIDI`) | `PORTMIDI_TESTS_ALL` | `portmidi_error_text_and_filters`: `Pm_GetErrorText` returns text; the clock, play and song-position filter bits are set and disjoint |
| `--alsa` (`FEAT_ALSA`, Linux only) | `ALSA_TESTS_ALL` | `alsa_version_and_open_modes`: `snd_asoundlib_version()` is non-empty; `SND_SEQ_OPEN_DUPLEX` is `SND_SEQ_OPEN_OUTPUT \| SND_SEQ_OPEN_INPUT`; `SND_SEQ_NONBLOCK` is non-zero |

The PortMidi build needs `libportmidi-dev`, so it runs in CI only, not on
the uConsole. The ALSA build needs `libasound2-dev`, which the uConsole has;
its test opens no sequencer, so it also runs where `/dev/snd/seq` is
missing, as on the CI runners:

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d --alsa test
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests
```

`-DUNIT_TESTS_CANARY` (through `CFLAGS_EXTRA`) adds
`unit_check_canary`, a test whose check is false; CI uses it to prove the
runner reports a failure and exits 1. `-DUNIT_TESTS_UNREGISTERED_CANARY`
defines `test_unregistered_canary` with its own prototype and in no list,
so it builds but never runs; CI uses it to prove that
`tests/check-debug-build.sh` names it. `./tool build test` stops with an
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
   list for its adapter flag (`PORTMIDI_TESTS_ALL`, `ALSA_TESTS_ALL`).
   `tests.h` generates every test's prototype from these lists, outside any
   `#ifdef`, and makes `-Wmissing-prototypes` an error, so a `test_<name>`
   that no list names fails to build instead of never running. A test that
   declares its own prototype gets past that, and
   `tests/check-debug-build.sh` (below) catches it by count. Make helper
   functions `static`.
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
  includes `<stdio.h>` and calls `rand()`; it is scanned first, the same
  way as a CORE file, and both must be reported, or the check is blind and
  the run fails. The `rand` half keeps that part of the check proven now
  that B1 has removed `rand()` from `sim.c` and deleted its marker.
- `tests/check-nm.sh` (spine AD-1) compiles each CORE file on its own with
  `gcc -c -std=c99 -O2 -DNDEBUG -g0 -fno-pie -no-pie -fno-lto` and fails
  each writable symbol (class `B b C D d G g S s V v`) that `nm -P` lists
  and no marker covers, and a file with no symbol at all. Markers name a
  symbol or a prefix; the marker set is empty since B3, so any writable
  global in a CORE file fails. The canary `tests/nm/canary.c` holds one
  writable symbol of each class `b`, `d`, `B` and `D`, each with its address
  escaping. On every run the check must flag all four by name, and its
  marker code must sort them with a built-in marker set:
  `canary_static_*` matches both statics, `canary_extern_init` its one
  symbol, `canary_absent` nothing (the XPASS path), and
  `canary_extern_zero`, which no marker covers, is reported unmarked.
  Otherwise the check is blind and the run fails. Symbols are listed in C
  collation. The script is Linux/ELF only: elsewhere it exits 2.

Today they print, on the uConsole's gcc 12.2:

```text
$ tests/check-includes.sh
PASS  canary (detected: <stdio.h>, rand)
PASS  gbuffer.c
PASS  vmio.c
PASS  sim.c
PASS  opstate.c
PASS  ccout.c
PASS  tick.c
include check: 6 files, 6 pass, 0 xfail, 0 fail, 0 xpass; canary detected
$ tests/check-nm.sh
PASS  canary (detected: D canary_extern_init, B canary_extern_zero, d canary_static_init, b canary_static_zero; marker path: canary_extern_zero unmarked, canary_absent matches none)
PASS  gbuffer.c
PASS  vmio.c
PASS  sim.c
PASS  opstate.c
PASS  ccout.c
PASS  tick.c
nm check: 6 files, 6 pass, 0 xfail, 0 fail, 0 xpass; canary detected
```

In the summaries, `pass` counts clean files, and `xfail`, `fail` and `xpass`
count results, so one file can add several.

### Check markers

`tests/xfail/<check>` lists the violations a check expects, one per line;
`#` starts a comment.

| Marker file | Line format | Today |
| --- | --- | --- |
| `tests/xfail/include-check` | `<file> <pattern> <item...>`, where the pattern is `rand`, `<name.h>` or `"name.h"` | none (B1 deleted `sim.c rand B1`) |
| `tests/xfail/nm-check` | `<file> <symbol\|glob> <item>`, where the pattern is a symbol name, or a literal prefix of at least 3 characters followed by one trailing `*` (`chord_*`) | none (B1 deleted its three lines and B3 its four) |

- The items are the spec items the fix waits for, each matching
  `^(B[1-8]|I[1-6])$` (`B1`, `I3`); an nm marker names exactly one. A
  marked violation reports XFAIL and the run stays green.
- A marker whose violation is gone reports XPASS and fails the run: the
  series that turns a check green deletes the marker line in the same
  commit.
- Each nm marker line reports on its own: XFAIL with its item and every
  writable symbol it matched (class and name), or XPASS once it matches
  none, so each item removes the lines of the symbols it deletes, as B1
  did. A writable symbol that no line of its file matches fails as
  `FAIL <file> <class> <name> (unmarked symbol)`.
- B3 made every lookup table in `sim.c` `const` at every pointer level and
  deleted the `chord_*` and `scale_*` lines, which covered the 72 tables
  that stayed writable because `scales` and `scales_and_chords` held their
  addresses. At `-O2` a table that nothing addresses or writes shows as `r`
  even when it is not `const`, because gcc places never-written statics in
  read-only data, so this recipe cannot prove AD-1's rule. B3 checked the
  declarations and recorded a one-off `-O0` `nm` of `sim.o` that lists no
  writable symbol. An XFAIL line names every symbol it matched, so a CI log
  records the runner gcc's list, which may differ from the uConsole's; that
  log is the evidence for any marker change.
- A marker that names a file outside CORE, a malformed line, any other nm
  pattern (`*_*`, `_*`, `sc*`, `sca?`) or a duplicate (file, pattern) fails
  the run. An nm marker never hides an object with no symbols or a failed
  compile.

### Debug-build check

`tests/check-debug-build.sh <binary> [runner-log]` fails a debug binary
whose symbol table (`nm`) lacks `__asan_init` or a `__ubsan_handle_*`
symbol, each of class `U` (gcc's shared runtimes) or `T` (a runtime linked
in statically, as clang does): `tool` only warns when it cannot detect the
compiler, and then builds without sanitizers, which no other step would
notice. `__ubsan_default_options`, which the runner defines, never counts.
Given the log of one `build/debug/unit_tests` run, it also fails unless
the run ended with `<n> tests, 0 failed`, the log holds no sanitizer line
(`Sanitizer` or `runtime error:`, including a LeakSanitizer report printed
after the summary), every `T test_*` function the binary defines is
reported by `ok` or `FAIL` (it names each one that is not), no test is
reported twice, and `<n>` equals the number of `T test_*` functions. A test
registered twice cannot hide an unregistered one by keeping the count
equal. Release and unsigned-char binaries are exempt and never checked:
they link with `-flto -s`, so `nm` lists nothing and the script fails them,
as it fails any stripped binary. The script is Linux/ELF only: macOS
prefixes symbol names with an underscore, so elsewhere it exits 2.

```sh
nice -n 19 taskset -c 0-2 tests/check-debug-build.sh build/debug/cli
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests 2>&1 | tee /tmp/unit_tests.log
nice -n 19 taskset -c 0-2 tests/check-debug-build.sh build/debug/unit_tests /tmp/unit_tests.log
```

Each `--portmidi` or `--alsa` test build overwrites
`build/debug/unit_tests`, so check each runner right after its own run.

## CI

`.github/workflows/ci.yml` runs on pushes and pull requests to
`spec/borca-fixes`; start a manual run with
`gh workflow run ci.yml --ref spec/borca-fixes`.

- **build** (pinned `ubuntu-24.04` and `ubuntu-24.04-arm`, never `-latest`):
  first checks that `CFLAGS_EXTRA` reaches the compiler, then builds the
  debug and release `cli` with `CFLAGS_EXTRA=-Werror`, checks the debug
  `cli` with `tests/check-debug-build.sh`, and runs the golden suite on
  both. It then runs `tests/check-includes.sh` and `tests/check-nm.sh`,
  which need no build, builds and runs the debug unit tests without and
  with `--portmidi` and with `--alsa` (ASan, UBSan with `halt_on_error=1`;
  the `--portmidi` run fails unless it prints
  `ok portmidi_error_text_and_filters`, the `--alsa` run unless it prints
  `ok alsa_version_and_open_modes`), and runs
  `tests/check-debug-build.sh` on each runner with its log before the next
  build replaces it. Three steps prove the checks' failure paths on every
  run: `tests/run.sh` must fail a throwaway `pitch_bend.xfail` holding
  `TODO`, then `B2 B5`, as a bad marker, and `--update` must leave an
  edited `pitch_bend.events` alone, after which the tree must be clean;
  `tests/check-debug-build.sh` must exit 1 on a program built with plain
  `cc`, naming both sanitizers; and a debug runner built with
  `-DUNIT_TESTS_UNREGISTERED_CANARY`, which defines
  `test_unregistered_canary` with its own prototype and in no list, must
  make it exit 1, naming that test. It checks that `tool` refuses
  `--alsa --portmidi` with `Choose ALSA or PortMidi`, then builds the debug
  `orca`, the debug `orca` with PortMidi (`make debug`) and with ALSA, the
  release `orca` without mouse support, the release `orca` with PortMidi,
  the release `orca` with ALSA and `-DBORCA_DEBUG_COUNTERS`, the release
  `orca` with ALSA and the appliance `orca` (`make appliance`, which is
  `--alsa --harden --pie`), all with `-Werror`. The plain ALSA and appliance
  binaries must list `libasound.so.2` as `NEEDED` in `readelf -d`; the
  appliance binary must also be of type `DYN` in `readelf -h` (PIE) and
  import `__stack_chk_fail` in `nm -D --undefined-only` (hardened). Both
  runners install `libasound2-dev` for the ALSA builds.
- **unsigned-char** (`ubuntu-24.04`): runs the golden suite on a release
  `cli`, and the release unit tests, both built with `-funsigned-char`;
  checks that a release runner built with `-DUNIT_TESTS_CANARY` exits 1 and
  prints `FAIL unit_check_canary:`; and runs
  `shellcheck -s sh tool tests/run.sh tests/check-includes.sh tests/check-nm.sh tests/check-debug-build.sh`.
- **armhf** (`ubuntu-24.04`, optional): only cross-compiles, with
  `arm-linux-gnueabihf-gcc`, each file in the union of `./tool sources cli`
  and `./tool sources test`; a red result does not fail the run.

`tool` adds `CFLAGS_EXTRA` after its own compiler flags, split on spaces. To
reproduce a red `build` or `unsigned-char` step on the uConsole:

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d cli
nice -n 19 taskset -c 0-2 tests/check-debug-build.sh build/debug/cli
nice -n 19 taskset -c 0-2 tests/run.sh build/debug/cli
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build cli
nice -n 19 taskset -c 0-2 tests/run.sh build/cli
nice -n 19 taskset -c 0-2 tests/check-includes.sh
nice -n 19 taskset -c 0-2 tests/check-nm.sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d test
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests 2>&1 | tee /tmp/unit_tests.log
nice -n 19 taskset -c 0-2 tests/check-debug-build.sh build/debug/unit_tests /tmp/unit_tests.log
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build test
nice -n 19 taskset -c 0-2 build/unit_tests
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d --alsa test
UBSAN_OPTIONS=halt_on_error=1 nice -n 19 taskset -c 0-2 build/debug/unit_tests 2>&1 | tee /tmp/unit_tests.log
nice -n 19 taskset -c 0-2 tests/check-debug-build.sh build/debug/unit_tests /tmp/unit_tests.log
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d orca
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build --no-mouse orca
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d --alsa orca
CFLAGS_EXTRA="-Werror -DBORCA_DEBUG_COUNTERS" nice -n 19 taskset -c 0-2 ./tool build --alsa orca
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build --alsa orca
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 make appliance
```

`-DBORCA_DEBUG_COUNTERS`, passed through `CFLAGS_EXTRA`, is a development
switch for `--alsa` builds: the ALSA output also counts the messages it drops
on `EAGAIN` or `EINTR`, and on quit, after the screen closes, prints
`bOrca ALSA: <n> MIDI sends dropped on EAGAIN/EINTR, <m> not sent in all` to
stderr (`<m>` counts every message that did not go out). It is never part of
a release or appliance build; CI only compiles it.

Building an `--alsa` `orca` is safe while the appliance runs; running one is
not, unless it uses another client name: the appliance router refuses to
route two `bOrca` clients. Run a test build with
`BORCA_ALSA_CLIENT_NAME=bOrca-test` (any name but `bOrca`), a scratch
`XDG_CONFIG_HOME` and a copy of a patch, or stop `borca-tty.service` first
with Ian's approval.

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
