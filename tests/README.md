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

`examples/upstream/<dir>/<name>.orca` follows the first row: its expected
files sit under `tests/expected/examples/upstream/<dir>/` (see
[Upstream examples](#upstream-examples)).

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
  the 60 s per-case timeout (applied when `timeout(1)` is on `PATH`) fails
  the run whatever the marker says, so the
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

No marked repro remains. `scale_inv` (`$3CA2`, `$3CA0`) and
`midichord_inv` (`=13CA.1`) waited for B2, and `midichord_vel` (`=13Caf1`,
`=13C0f1`, `=13Ca01`) for B5; each item deleted the markers that waited
for it, and the hand-written goldens now pass unchanged. `midichord_inv`
used velocity `.` so that B5 could not change it; CAP-7's literal
`=13CAf1`, which both items changed, is checked by the unit test
`music_midichord_inversion`, its notes and, since B5, its velocity of 119.
When these fixed goldens were written, a throwaway script derived them
from the current output and asserted each line it edited; the script is
not committed.

### Goldens that record current behaviour on purpose

`r_single` and `r_shared` record lowercase `r` as B6 left it. Each `r`
keeps its own bag in the op-state store and shuffles it with the PCG32 in
`prng.h`, seeded from `--seed`, its row and its column, so the goldens hold
the same stream on every platform, and `r_shared`'s `0r3` emits exactly
`r_single`'s sequence. A bag that would start with the value just sent
swaps its first slot with a random other slot, so neither golden sends a
value twice in a row. They were replaced, not marked expected-fail, twice:
B1 replaced the glibc `rand()` stream they held before, when it removed
`rand()` from `sim.c` (spine AD-1), and B6 replaced them again when it fixed
`r`; only the `0r3` column changed, from tick 28, where its eighth bag
started with the value just sent. The unit tests `sim_r_*` in
`test_sim_state.c` prove no-repeat and permutation over every range from
`0r1` to `0rz`, and range change, seed, case, single value and first visit
on fixed patches.

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
operator (B1 and B6 changed it; `r` appears only as a `$` or `=` selector,
which those operators lock), no uppercase `$` or `=` selector (B2 changed
them; `examples/misc/chord_inversions.orca` and `tests/unit/test_music.c`
cover them), and no `=` velocity other than `.` or `z`, which gave 127
before and after B5. Every `&` and `;` cell keeps y × width + x below 4096:
before B1, `&` indexed its state with no bounds check and `;` returned early
from cell 4096 on. Each case was captured with `--update`. When the cases
were written, a throwaway model of the operators' source checked each cell;
it is not committed.

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

### Upstream examples

The five patches in `examples/upstream/` (`basics/_osc`, `basics/_udp`,
`benchmarks/io`, `misc/udp+loop` and `setups/knobs`) are upstream Orca-c
examples, byte-identical to Orca-c `9df9786`'s and at its relative paths
(`examples/upstream/README.md`). They use upstream's OSC, UDP and CC
operators, which bOrca's dialect replaces, so they run in the upstream
dialect (`--dialect upstream`). Each has two files under
`tests/expected/examples/upstream/<dir>/`:

- `<name>.events`: Orca-c `9df9786`'s own `cli --events -t 96 --seed 0`
  output, 96 ticks long because CAP-13 compares 96 ticks against Orca-c;
- `<name>.args`: `-t 96 --dialect upstream`.

They pass: bOrca's upstream dialect prints exactly what Orca-c does. Each
golden holds event lines, which is the proof that every patch under
`examples/upstream/` does something in the upstream dialect (CAP-13;
`tests/check-examples.sh` covers the rest of `examples/` in bOrca's).
Until I1 added the dialect, `cli` rejected `--dialect upstream` and each case
carried an `I1` marker; I1 deleted the markers.

`benchmarks/io` drives `:`, `!`, `;`, `=`, `?` and `$` from clocked tracks.
Its bOrca-dialect coverage stays in `tests/patches/io_borca.orca`, a copy of
the patch, whose `tests/expected/patches/io_borca.events` is the golden
`benchmarks/io` had before the move, unchanged.

The unit tests `dialect_*` in `tests/unit/test_dialect.c` check the rest of
the dialect on small grids: a context that names no dialect runs bOrca's
list; upstream `!` sends a plain CC from three inputs at value × 127 / 35;
upstream `r` is a banged `R`, cell for cell and tick for tick; upstream `;`
locks and sends at most 16 glyphs up to a `.`; upstream `=` sends its path
and values; both stop at the grid's east edge; upstream `$` and `&` write,
mark and keep nothing; the tick body runs `Tick_ctx`'s dialect, so upstream
`;` and `=` reach the sink's `osc` and upstream `!` goes out as a plain CC;
and only `borca` and `upstream` are dialect names.

### The upstream comparison

`tests/upstream/` holds an Orca-c build, which made the goldens above, and
the check that compares bOrca's upstream dialect with it (spine AD-21):

- `orca-c-cli.patch` adds `--events` and `--seed` to Orca-c's `cli_main.c`,
  with the output and parsing of bOrca's `cli`, and `events_print.c` to the
  `cli` sources in Orca-c's `tool`;
- `build-cli.sh` gets Orca-c at `9df9786e2ad3c01955cdf4cdd5ae1fffad8fa5cc`
  with `git archive`: from this repository when it has the commit (as when
  the `upstream` remote, hundredrabbits/Orca-c, is fetched), or else from a
  depth-1 fetch of that commit by URL from
  `https://github.com/hundredrabbits/Orca-c.git` into
  `build/upstream/orca-c.git`, which later runs reuse. It extracts the tree
  to `build/upstream/orca-c/`, copies this repository's `events_print.c` and
  `events_print.h` into it, so both `cli`s print through one file, applies
  the patch with `patch -p1`, and builds `build/upstream/cli` with Orca-c's
  own `tool`. It needs git, tar, patch and a C compiler.
- `compare.sh <bOrca cli>` needs that build, and checks, reporting each
  failure and carrying on:
  1. the tree's `events_print.c` and `events_print.h` are this repository's;
  2. every `examples/**/*.orca` of the Orca-c tree, 43 files: Orca-c's
     `cli --events -t 96 --seed 0` and bOrca's, with `--dialect upstream`
     added, print byte-identical events and grids, and both exit 0 with no
     sanitizer report. Fewer than 43 files fails;
  3. each `examples/upstream/<rel>.orca` is Orca-c's `examples/<rel>.orca`,
     and its golden is what Orca-c's `cli` prints for it, so neither can
     drift from Orca-c, and holds at least one event line;
  4. its list of the six exceptions is exactly the paths of the list in
     `tests/check-examples.sh`, so the two change together, and each is
     Orca-c's file at the same path.

  It exits 0 only when every check passes, 1 otherwise, and 2 on usage or
  when the Orca-c build is missing. CI runs it in both `build` legs, on the
  debug and the release `cli`, and proves it can fail with two canaries: the
  release `cli` with `--dialect borca` appended, and an upstream golden with
  a byte appended. To remake a golden, write the Orca-c `cli`'s
  output over it (`build/upstream/cli --events -t 96 --seed 0
  examples/upstream/<rel>.orca >tests/expected/examples/upstream/<rel>.events`).

```text
$ nice -n 19 taskset -c 0-2 tests/upstream/build-cli.sh
…
Built …/build/upstream/cli
$ nice -n 19 taskset -c 0-2 tests/upstream/compare.sh build/cli
PASS  events_print.c is the Orca-c build's copy
…
PASS  examples/basics/_midi.orca
…
upstream check: 43 examples, 43 identical; 5 goldens, 5 match; 13 files, 13 match; 0 fail
```

## README tables and examples

### readme-gen and doc-sync

`tools/readme-gen.c` prints the Markdown between `README.md`'s
`<!-- tables:begin -->` and `<!-- tables:end -->` markers: the scale, chord
and enriched-chord tables, with their headings and header rows and one blank
line between tables (spine AD-19). It reads `music.h` only through
`music_selector_name` and `music_decode` and copies no table, so the README
cannot drift from what `$` and `=` play. Note names come from one spelling
map per table: C Db D Eb E F Gb G G# A Bb B for 0 to 11, continuing an
octave up. The maps differ at 15, which is D# in the chord table (Dom7#9) and
Eb in the enriched table (Minor7+Oct3rd). A semitone outside its map, a
selector with no name or a failed write exits 1. `./tool build readme-gen`
builds CORE plus that file into `build/readme-gen` (`build/debug/readme-gen`
with `-d`).

`tests/check-doc-sync.sh <readme-gen> [README]` (the README defaults to the
repository's) requires exactly one line of each marker, each alone on its
line and begin first, and rejects a readme-gen run that fails or prints
nothing. It then `diff -u`s the lines between the markers (`-`) with
readme-gen's output (`+`), and exits 0 only when they are identical; 1 for a
difference or any of the failures above, 2 on usage. After a change to the
tables in `music.c`, replace the lines between the markers with
`build/readme-gen`'s output.

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build readme-gen
tests/check-doc-sync.sh build/readme-gen
```

### Non-empty check of the examples

`tests/check-examples.sh <cli>` runs every `examples/**/*.orca` outside
`examples/upstream/` through `cli --events -t 64 --seed 0`, and requires each
to do something in bOrca's dialect (`amendments.md` B7):

- a file that contains an event operator glyph (`!`, `%`, `:`, `=` or `?`,
  anywhere in the file) passes on at least one event line;
- any other file passes on at least one tick whose grid differs from the
  file as loaded, which is the grid `cli -t 0 <file>` prints (it adds the
  final newline that many example files lack).

Six named exceptions fail that rule on purpose. The script lists each with
its reason, reports it as EXCEPT, and they stay byte-identical to upstream
Orca-c's examples, which `tests/upstream/compare.sh` checks:

| Exception | Why it fails the rule |
| --- | --- |
| `examples/basics/a.orca` | its `A` outputs already hold their sums, so no tick changes the grid |
| `examples/basics/k.orca` | its `K` outputs already hold the variables, so no tick changes the grid |
| `examples/basics/l.orca` | its `L` outputs already hold the lesser inputs, so no tick changes the grid |
| `examples/misc/multiplication.orca` | its `K` and `O` outputs already hold their values, so no tick changes the grid |
| `examples/misc/colors.orca` | its `:` is never banged (and is a locked `$` input), so it sends nothing |
| `examples/setups/sequencer.orca` | an empty template: its `:` are banged every tick, but every note input is empty, so it sends nothing |

Any other failing file fails the run. So does a listed exception that
passes, or that names a missing file or one under `examples/upstream/`, so
the list cannot go stale. A non-zero `cli` exit, a sanitizer report, a
signal or the 60 s per-run timeout (applied when `timeout(1)` is on `PATH`)
fails the file, exception or not, as does output that is not 64 ticks of
event lines and grids. The script exits 0 only when every file passes or is
a listed exception. An example run, with B7's examples:

```text
$ nice -n 19 taskset -c 0-2 tests/check-examples.sh build/debug/cli
PASS   examples/basics/_midi.orca (26 event lines)
EXCEPT examples/basics/a.orca (no grid change: its A outputs already hold their sums, so no tick changes the grid)
…
examples check: 44 files, 38 pass, 6 exceptions, 0 fail
```

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
opstate.c ccout.c music.c tick.c`; the other extracted core modules join it
later).
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
PASS  music.c
PASS  tick.c
include check: 7 files, 7 pass, 0 xfail, 0 fail, 0 xpass; canary detected
$ tests/check-nm.sh
PASS  canary (detected: D canary_extern_init, B canary_extern_zero, d canary_static_init, b canary_static_zero; marker path: canary_extern_zero unmarked, canary_absent matches none)
PASS  gbuffer.c
PASS  vmio.c
PASS  sim.c
PASS  opstate.c
PASS  ccout.c
PASS  music.c
PASS  tick.c
nm check: 7 files, 7 pass, 0 xfail, 0 fail, 0 xpass; canary detected
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
  writable symbol. B2 moved the scale and chord tables to `music.c`, and a
  one-off `-O0` `nm` of `music.o`, with this recipe's other flags, lists no
  writable symbol either. An XFAIL line names every symbol it matched, so a
  CI log records the runner gcc's list, which may differ from the
  uConsole's; that log is the evidence for any marker change.
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
`spec/borca-fixes`, the working branch, and `main`, bOrca's release line
from `v1.0.0`, and on `v*` tags; start a manual run with
`gh workflow run ci.yml --ref <branch>`.

- **build** (pinned `ubuntu-24.04` and `ubuntu-24.04-arm`, never `-latest`):
  first checks that `CFLAGS_EXTRA` reaches the compiler, then builds the
  debug and release `cli` with `CFLAGS_EXTRA=-Werror`, checks the debug
  `cli` with `tests/check-debug-build.sh`, and runs the golden suite on
  both. It runs `tests/check-examples.sh` on the debug `cli`, builds
  `readme-gen` with `-Werror` and runs `tests/check-doc-sync.sh` on it. It
  then runs `tests/check-includes.sh` and `tests/check-nm.sh`,
  which need no build, builds and runs the debug unit tests without and
  with `--portmidi` and with `--alsa` (ASan, UBSan with `halt_on_error=1`;
  the `--portmidi` run fails unless it prints
  `ok portmidi_error_text_and_filters`, the `--alsa` run unless it prints
  `ok alsa_version_and_open_modes`), and runs
  `tests/check-debug-build.sh` on each runner with its log before the next
  build replaces it. Five steps prove the checks' failure paths on every
  run: `tests/run.sh` must fail a throwaway `pitch_bend.xfail` holding
  `TODO`, then `B2 B5`, as a bad marker, and `--update` must leave an
  edited `pitch_bend.events` alone, after which the tree must be clean;
  `tests/check-examples.sh` must exit 1 with a FAIL line for a throwaway
  dots-only `examples/ci-canary.orca`, after which the tree must be clean;
  `tests/check-doc-sync.sh` must exit 1 on a README copy under
  `$RUNNER_TEMP` whose `k/K` row has one cell changed, printing that row,
  and on one with its end marker deleted, as bad markers;
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
  `shellcheck -s sh` over `tool`, `tests/run.sh`, `tests/check-includes.sh`,
  `tests/check-nm.sh`, `tests/check-debug-build.sh`,
  `tests/check-doc-sync.sh`, `tests/check-examples.sh` and
  `tests/upstream/build-cli.sh`.
- **armhf** (`ubuntu-24.04`, optional): only cross-compiles, with
  `arm-linux-gnueabihf-gcc`, each file in the union of `./tool sources cli`,
  `./tool sources test` and `./tool sources readme-gen`; a red result does
  not fail the run.

`tool` adds `CFLAGS_EXTRA` after its own compiler flags, split on spaces. To
reproduce a red `build` or `unsigned-char` step on the uConsole:

```sh
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build -d cli
nice -n 19 taskset -c 0-2 tests/check-debug-build.sh build/debug/cli
nice -n 19 taskset -c 0-2 tests/run.sh build/debug/cli
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build cli
nice -n 19 taskset -c 0-2 tests/run.sh build/cli
nice -n 19 taskset -c 0-2 tests/check-examples.sh build/debug/cli
CFLAGS_EXTRA=-Werror nice -n 19 taskset -c 0-2 ./tool build readme-gen
tests/check-doc-sync.sh build/readme-gen
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
