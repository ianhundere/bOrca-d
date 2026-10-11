# Upstream Orca-c examples

These five patches are upstream Orca-c examples, unchanged from
[hundredrabbits/Orca-c](https://github.com/hundredrabbits/Orca-c) and kept at
Orca-c's paths below this directory. They are written for upstream's
operators, which bOrca's dialect replaces, so in bOrca they play nothing or
the wrong thing. They are meant for `--dialect upstream`, the upstream
operator dialect that a later bOrca release adds (spec item I1); bOrca does
not accept it yet.

| Patch | Upstream operators it uses | What bOrca plays instead |
| --- | --- | --- |
| `basics/_osc.orca` | `=` sends OSC | `=` is the Midichord operator |
| `basics/_udp.orca` | `;` sends UDP | `;` is the arpeggiator |
| `misc/udp+loop.orca` | `;` sends UDP | `;` is the arpeggiator |
| `setups/knobs.orca` | `!` sends a CC from channel, control and a value scaled to 0-127 | `!` reads a three-digit control number, a value times 4 and a glide rate |
| `benchmarks/io.orca` | `:`, `!`, `;`, `=` and `?` side by side (`$` is not an operator upstream) | bOrca's operators of the same glyphs, `$` included |

bOrca has no OSC or UDP operators. Its own examples are the other
directories under `examples/`.

## Tests

Their goldens under `tests/expected/examples/upstream/` are upstream Orca-c
`9df9786`'s own output over 96 ticks, from the Orca-c `cli` that
`tests/upstream/build-cli.sh` builds (see `tests/README.md`). Each case runs
with `--dialect upstream` and is marked as waiting for I1, so it reports
XFAIL until I1 makes bOrca's output match. bOrca's dialect keeps its own
coverage of `benchmarks/io.orca` in `tests/patches/io_borca.orca`, a copy
with the golden it had before the move.
