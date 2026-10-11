# Upstream Orca-c examples

These five patches are upstream Orca-c examples, unchanged from
[hundredrabbits/Orca-c](https://github.com/hundredrabbits/Orca-c) and kept at
Orca-c's paths below this directory. They are written for upstream's
operators, so play them in bOrca's upstream operator dialect (see Dialects in
the [README](../../README.md#dialects)): pick Upstream under Operator
Dialect... in the main menu (Ctrl+D), or start `orca --dialect upstream`, or
run them headless with `cli --dialect upstream`. In bOrca's own dialect, the
default, the same glyphs are other operators, so there they play nothing or
the wrong thing:

| Patch | Upstream operators it uses | What bOrca's dialect plays instead |
| --- | --- | --- |
| `basics/_osc.orca` | `=` sends OSC | `=` is the Midichord operator |
| `basics/_udp.orca` | `;` sends UDP | `;` is the arpeggiator |
| `misc/udp+loop.orca` | `;` sends UDP | `;` is the arpeggiator |
| `setups/knobs.orca` | `!` sends a CC from channel, control and a value scaled to 0-127 | `!` reads a three-digit control number, a value times 4 and a glide rate |
| `benchmarks/io.orca` | `:`, `!`, `;`, `=` and `?` side by side (`$` is not an operator upstream) | bOrca's operators of the same glyphs, `$` included |

In the upstream dialect `=` and `;` send OSC and UDP to the address and port
set under OSC Output in the main menu, and nothing while OSC output is off.
bOrca's own examples are the other directories under `examples/`.

## Tests

Their goldens under `tests/expected/examples/upstream/` are upstream Orca-c
`9df9786`'s own output over 96 ticks, from the Orca-c `cli` that
`tests/upstream/build-cli.sh` builds (see `tests/README.md`). Each case runs
with `--dialect upstream`, and bOrca's output matches them byte for byte;
each holds event lines, so every patch here does something in the upstream
dialect. CI also checks that these files and their goldens are still
Orca-c's, and compares the upstream dialect with Orca-c on all 43 of its
examples. bOrca's dialect keeps its own coverage of `benchmarks/io.orca` in
`tests/patches/io_borca.orca`, a copy with the golden it had before the move.
