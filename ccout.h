#pragma once
#include "base.h"
#include "vmio.h"

// The CC engine (architecture spine AD-9, AD-10, AD-13): every CC from the
// tick path goes through it. It owns the glides, the last value sent on each
// controller, the glide_hz step limit, the de-duplication of glide steps and
// the budget's check-and-debit point, and it is the only place a `!` rate
// index maps to a duration. It is core: it reads no clock, does no I/O and
// keeps no writable globals. The shell holds one engine by value (Ged), and
// tests build theirs on the stack.
//
// Times are U64 microseconds of the shell's one clock (now_us() in
// tui_main.c). Output is Oevent_midi_cc items appended to the caller's list,
// and the shell sends every one: "sent" means emitted by the engine.
//
// Instants. An Oevent_midi_cc, or a `!` with rate index 0 or 35 (`0`, `z`),
// is instant: it always goes out, even when it equals the last value, updates
// the last value and cancels an active glide on its controller.
//
// Glides. Rates 1-34 glide from the current value to the target over
// (36 - rate) ticks: t_end = submit time + (36 - rate) * tick_len, fixed at
// submit, so a later tempo change does not alter it. The current value is
// the glide's interpolant if one is active, otherwise the last value sent.
// A retarget's start keeps Ccout_frac_bits fractional bits of the
// interpolant, floored, so it continues from the glide's progress to within
// 2^-16, not from a rounded value; a value is rounded half up, as 4f349cd's
// (U8)(v + 0.5), only when it goes out or is compared. In integer
// arithmetic, with d = t_end - t0 and el = now - t0 clamped to [0, d], the
// interpolant is start + floor((target - start) * el / d).
//
// A poll before t_end sends a step only if at least 1 000 000 / glide_hz us
// have passed since that controller's last emission (none if now is before
// it) and the step's value differs from the last value. The first poll at or
// after t_end sends the target unless the last value already equals it;
// glide_hz never holds the target back. A glide ends at its first emission
// equal to its target, at t_end, or by a cancel, an instant or a retarget.
//
// A glide submit takes the first of these that applies:
//   1. rate index 0 or 35: an instant;
//   2. the last value is unknown: the target goes out at once, as an instant
//      would, cancelling any active glide (AD-10);
//   3. the same target and rate as the active glide: nothing changes, and
//      tick_len is ignored (the glide keeps its start and t_end);
//   4. the target equals the current value: any active glide is cancelled
//      and nothing goes out (AD-9);
//   5. otherwise a new glide, or a retarget of the active one, from the
//      current value.
//
// The budget. Every emission passes one internal check-and-debit point:
// steps need a token, instants and target emissions always debit. It never
// limits yet; I2 fills it with the token bucket without changing this API.
//
// Debug builds assert ch <= 15, cc <= 127, values <= 127 and rate_idx <= 35.

enum {
  Ccout_frac_bits = 16, // the fractional bits of a glide's start
  Ccout_channels = 16,
  Ccout_controls = 128,
  Ccout_slots = Ccout_channels * Ccout_controls, // channel * 128 + cc
  Ccout_glide_hz_default = 100,
};

typedef struct {
  U64 t0, t_end; // the glide's start and end, us
  U32 start;     // the start value, 0-127, with Ccout_frac_bits fraction bits
  U8 target;     // 0-127
  U8 rate;       // the rate index, 1-34
  bool active;
} Ccout_glide;

// Fixed size and no heap: about 67.6 KB. ccout_init sets every field.
typedef struct {
  Ccout_glide glides[Ccout_slots];        // indexed by channel * 128 + cc
  U64 last_emit[Ccout_slots];             // UINT64_MAX if none
  I8 last_value[Ccout_channels][Ccout_controls]; // -1 is unknown
  Usz active_count;                       // glides with active set
  Usz glide_hz;
  U64 step_gap; // ceil(1 000 000 / glide_hz): the shortest gap before a step
} Ccout_engine;

// No glide active, every last value unknown, no emission times, and glide_hz
// at its default, 100.
void ccout_init(Ccout_engine *engine);

// Sets the per-controller step limit, in steps per second; 0 means the
// default, 100. The shell configures 100 at init.
void ccout_configure(Ccout_engine *engine, Usz glide_hz);

// An instant CC: an Oevent_midi_cc, or a CCI at rate 0 or 35.
void ccout_submit_instant(Ccout_engine *engine, U8 ch, U8 cc, U8 value, U64 now,
                          Oevent_list *out);

// A CCI: the rules 1-5 above. tick_len is one tick in us; 0 counts as 1,
// and more than 2^32 us (about 71.6 minutes) counts as 2^32, which keeps the
// integer maths in range.
void ccout_submit_glide(Ccout_engine *engine, U8 ch, U8 cc, U8 target,
                        U8 rate_idx, U64 tick_len, U64 now, Oevent_list *out);

// Advances every active glide to now_us, emitting in slot order (channel,
// then cc). A poll that emits nothing and ends no glide changes nothing.
void ccout_poll(Ccout_engine *engine, U64 now_us, Oevent_list *out);

// False when no glide is active. Otherwise sets *t to the earliest time, never
// before now, at which ccout_poll would emit or end a glide: a poll at *t
// does, and a poll from now up to *t - 1 does neither.
bool ccout_next_deadline(Ccout_engine const *engine, U64 now, U64 *t);

// Sets every last value to unknown, so the next glide on each controller
// jumps to its target. Glides and emission times are left alone, so an
// active glide continues. Used when the output changes, and on open and new.
void ccout_forget(Ccout_engine *engine);

// Ends every active glide, keeping the last values and emission times. Used
// on pause, open and new.
void ccout_cancel(Ccout_engine *engine);
