// Unit tests for ccout.c, the CC engine (spec item B3, CAP-8; spine AD-9,
// AD-10): one test or more for each row of the story's I/O matrix, at the
// API level, on engines built on the stack. The tick-level rows (rates 0 and
// z on the wire, the step-3 poll order) are in test_tick.c. The pause row is
// ged_set_playing's ccout_cancel, checked by code reading together with
// ccout_cancel_keeps_last_value.
//
// Rate w (index 32) glides over 36 - 32 = 4 ticks, and one tick is 125 000 us
// at 120 BPM, so a glide at w lasts 500 000 us.
#include "../../ccout.h"
#include "../../vmio.h"
#include "tests.h"

enum { Ch = 0, Cc = 74, Rate_w = 32, Seq_capacity = 128 };

static U64 const tick_us = 125000;

// The CCs that a run of polls (or submits) emitted on (Ch, Cc), with their
// times. Anything on another controller, or past the capacity, sets bad.
typedef struct {
  U8 values[Seq_capacity];
  U64 times[Seq_capacity];
  Usz count;
  bool bad;
} Seq;

static void seq_take(Seq *seq, Oevent_list const *out, U64 now) {
  for (Usz i = 0; i < out->count; ++i) {
    Oevent const *e = &out->buffer[i];
    if (e->any.oevent_type != Oevent_type_midi_cc ||
        e->midi_cc.channel != Ch || e->midi_cc.control != Cc ||
        seq->count == Seq_capacity) {
      seq->bad = true;
      continue;
    }
    seq->values[seq->count] = e->midi_cc.value;
    seq->times[seq->count] = now;
    ++seq->count;
  }
}

static void seq_clear(Seq *seq) { memset(seq, 0, sizeof *seq); }

// The run emitted exactly these values, in order.
static bool seq_is(Seq const *seq, U8 const *want, Usz count) {
  if (seq->bad || seq->count != count)
    return false;
  for (Usz i = 0; i < count; ++i) {
    if (seq->values[i] != want[i])
      return false;
  }
  return true;
}

// Polls at now into a cleared out, and records what it emitted.
static void poll_at(Ccout_engine *e, U64 now, Oevent_list *out, Seq *seq) {
  oevent_list_clear(out);
  ccout_poll(e, now, out);
  seq_take(seq, out, now);
}

// Submits into a cleared out, and records what it emitted.
static void instant_at(Ccout_engine *e, U8 value, U64 now, Oevent_list *out,
                       Seq *seq) {
  oevent_list_clear(out);
  ccout_submit_instant(e, Ch, Cc, value, now, out);
  seq_take(seq, out, now);
}

static void glide_at(Ccout_engine *e, U8 target, U8 rate, U64 tick_len,
                     U64 now, Oevent_list *out, Seq *seq) {
  oevent_list_clear(out);
  ccout_submit_glide(e, Ch, Cc, target, rate, tick_len, now, out);
  seq_take(seq, out, now);
}

static bool is_idle(Ccout_engine const *e) {
  U64 t;
  return !ccout_next_deadline(e, 0, &t) && e->active_count == 0;
}

// Matrix row "Continuity", and the spec Appendix's "advance 5 times" test
// rewritten against ccout_poll: two glides on channel 0, CC 74, rate 32 (w),
// to 64 and then, after the first completes, to 80, each polled 5 times, a
// tick apart. 4f349cd sent 16, 32, 48, 64, then 20, 40, 60, 80: the second
// glide restarted from 0. The engine first learns that the controller is at
// 0 from an instant; with no last value, a glide jumps to its target.
void test_ccout_glide_continuity(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  U8 const want_instant[] = {0};
  CHECK(seq_is(&seq, want_instant, 1));

  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  for (U64 k = 1; k <= 5; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want_first[] = {16, 32, 48, 64};
  CHECK(seq_is(&seq, want_first, ORCA_ARRAY_COUNTOF(want_first)));
  CHECK(seq.count == 4 && seq.times[3] == 4 * tick_us);
  CHECK(is_idle(&e));

  seq_clear(&seq);
  glide_at(&e, 80, Rate_w, tick_us, 5 * tick_us, &out, &seq);
  for (U64 k = 6; k <= 10; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want_second[] = {68, 72, 76, 80};
  CHECK(seq_is(&seq, want_second, ORCA_ARRAY_COUNTOF(want_second)));
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Matrix row "From an instant": instant 100, then a glide to 50 at w. The
// falling values round half up (87.5 gives 88, 62.5 gives 63).
void test_ccout_glide_from_instant(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 100, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 50, Rate_w, tick_us, 0, &out, &seq);
  for (U64 k = 1; k <= 5; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {88, 75, 63, 50};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Matrix row "Unknown last value": on a fresh engine a glide sends its
// target at once, from the submit, and starts no glide.
void test_ccout_unknown_last_value(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 1000, &out, &seq);
  U8 const want[] = {64};
  CHECK(seq_is(&seq, want, 1));
  CHECK(is_idle(&e));
  for (U64 k = 1; k <= 5; ++k)
    poll_at(&e, 1000 + k * tick_us, &out, &seq);
  CHECK(seq_is(&seq, want, 1)); // the polls added nothing
  oevent_list_deinit(&out);
}

// Rates 0 and 35 (`0`, `z`) are instant, judged by the index: the value goes
// out at once, even when it equals the last value, and cancels a running
// glide. The tick-level row is test_tick.c's tick_cci_same_tick.
void test_ccout_instant_rates(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 10, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  CHECK(!is_idle(&e));
  glide_at(&e, 20, 35, tick_us, 1000, &out, &seq); // rate z: at once
  CHECK(is_idle(&e));                              // and the glide ended
  glide_at(&e, 20, 0, tick_us, 2000, &out, &seq);  // rate 0, the same value
  glide_at(&e, 20, 35, tick_us, 3000, &out, &seq); // rate z, again
  for (U64 k = 1; k <= 5; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {10, 20, 20, 20};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(seq.count == 4 && seq.times[1] == 1000 && seq.times[2] == 2000 &&
        seq.times[3] == 3000);
  oevent_list_deinit(&out);
}

// Matrix row "Instant cancels": an instant during a glide goes out, and the
// glide emits nothing more.
void test_ccout_instant_cancels_glide(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, tick_us, &out, &seq);
  instant_at(&e, 100, tick_us + 1000, &out, &seq);
  CHECK(is_idle(&e));
  for (U64 k = 2; k <= 6; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {0, 16, 100};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  oevent_list_deinit(&out);
}

// Matrix row "No-op": idle at 64, a glide to 64 sends nothing and starts
// nothing.
void test_ccout_noop_submit(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 64, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, tick_us, &out, &seq);
  CHECK(is_idle(&e));
  for (U64 k = 2; k <= 6; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {64};
  CHECK(seq_is(&seq, want, 1));
  oevent_list_deinit(&out);
}

// Matrix row "No-op, step held back": glide_hz holds the step to 1 at
// t0 + 5 000, so a glide to 1 then equals the current value (the
// interpolant): it cancels the glide and sends nothing. The wire stays at 0
// (AD-9 as written; the spine defect is reported in the story).
void test_ccout_noop_step_held_back(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, 5000, &out, &seq);
  glide_at(&e, 1, Rate_w, tick_us, 5000, &out, &seq);
  CHECK(is_idle(&e));
  for (U64 k = 1; k <= 5; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {0};
  CHECK(seq_is(&seq, want, 1));
  oevent_list_deinit(&out);
}

// Matrix row "Early end": 64 to 65 at w reaches 65 at the second poll, which
// ends the glide, so nothing goes out at t_end.
void test_ccout_early_end(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 64, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 65, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, tick_us, &out, &seq);
  CHECK(seq.count == 0);
  poll_at(&e, 2 * tick_us, &out, &seq);
  CHECK(is_idle(&e));
  poll_at(&e, 3 * tick_us, &out, &seq);
  poll_at(&e, 4 * tick_us, &out, &seq);
  U8 const want[] = {65};
  CHECK(seq_is(&seq, want, 1));
  CHECK(seq.count == 1 && seq.times[0] == 2 * tick_us);
  oevent_list_deinit(&out);
}

// Matrix row "De-dup": 0 to 2 at rate 28 (8 ticks), polled each tick, sends
// exactly 1, then 2. 4f349cd sent 0, 1, 1, 1, 1, 2, 2, 2.
void test_ccout_dedup(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 2, 28, tick_us, 0, &out, &seq);
  for (U64 k = 1; k <= 8; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {1, 2};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(seq.count == 2 && seq.times[0] == 2 * tick_us &&
        seq.times[1] == 6 * tick_us);
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// glide_hz: instant 0 at t0, then 0 to 127 at rate 34 (2 ticks, 250 000 us),
// polled every 1 ms. Checks the count, the gaps (from the instant on), that
// the values rise strictly, and the target at t_end.
static void run_glide_hz(Usz glide_hz, Usz want_count, U64 want_gap) {
  enum { T0 = 1000000 };
  Ccout_engine e;
  ccout_init(&e);
  ccout_configure(&e, glide_hz);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, T0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 127, 34, tick_us, T0, &out, &seq);
  for (U64 t = T0 + 1000; t <= T0 + 260000; t += 1000)
    poll_at(&e, t, &out, &seq);
  CHECK(!seq.bad && seq.count == want_count);
  bool gaps_ok = true, rising = true;
  for (Usz i = 0; i < seq.count; ++i) {
    U64 prev_t = i == 0 ? T0 : seq.times[i - 1];
    if (seq.times[i] - prev_t < want_gap)
      gaps_ok = false;
    if (i > 0 && seq.values[i] <= seq.values[i - 1])
      rising = false;
  }
  CHECK(gaps_ok);
  CHECK(rising);
  CHECK(seq.count > 0 && seq.values[seq.count - 1] == 127 &&
        seq.times[seq.count - 1] == T0 + 250000);
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Matrix row "glide_hz": 25 emissions at least 10 000 us apart at the
// default, 100; 50 at least 5 000 us apart at 200.
void test_ccout_glide_hz(void) {
  run_glide_hz(100, 25, 10000);
  run_glide_hz(200, 50, 5000);
  run_glide_hz(0, 25, 10000); // 0 means the default
}

// Matrix row "Fixed length": the same glide at two tick lengths, each polled
// once at t0 + 500 000: at 125 000 us a tick it has ended at 64; at 250 000
// it is halfway, at 32.
void test_ccout_fixed_length(void) {
  U64 const tick_lens[] = {125000, 250000};
  U8 const want[] = {64, 32};
  for (Usz i = 0; i < 2; ++i) {
    Ccout_engine e;
    ccout_init(&e);
    Oevent_list out;
    oevent_list_init(&out);
    Seq seq;
    seq_clear(&seq);
    instant_at(&e, 0, 0, &out, &seq);
    seq_clear(&seq);
    glide_at(&e, 64, Rate_w, tick_lens[i], 0, &out, &seq);
    poll_at(&e, 500000, &out, &seq);
    CHECK(seq_is(&seq, want + i, 1));
    oevent_list_deinit(&out);
  }
}

// Matrix row "Retarget": a glide to 80 after the second poll of 0 to 64 at w
// starts from the interpolant, 32, and takes 4 more ticks.
void test_ccout_retarget(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, tick_us, &out, &seq);
  poll_at(&e, 2 * tick_us, &out, &seq);
  glide_at(&e, 80, Rate_w, tick_us, 2 * tick_us, &out, &seq);
  for (U64 k = 3; k <= 7; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {16, 32, 44, 56, 68, 80};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Matrix row "Same glide again": 0 to 64 at w, submitted again on every
// tick after the poll, as the tick path does, runs undisturbed. Restarting
// it on each bang sent 16, 28, 37, 44 ... and reached 64 after about 17
// ticks.
void test_ccout_same_glide_again(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  for (U64 k = 1; k <= 8; ++k) {
    poll_at(&e, k * tick_us, &out, &seq);
    glide_at(&e, 64, Rate_w, tick_us, k * tick_us, &out, &seq);
  }
  U8 const want[] = {16, 32, 48, 64};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Rule 3 needs the same rate as well as the same target: 64 submitted
// again at rate 28 (8 ticks) after the first poll of 0 to 64 at w is a
// retarget from 16, which then rises by 6 a tick.
void test_ccout_same_target_new_rate(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, tick_us, &out, &seq);
  glide_at(&e, 64, 28, tick_us, tick_us, &out, &seq);
  for (U64 k = 2; k <= 10; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {16, 22, 28, 34, 40, 46, 52, 58, 64};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(seq.count == 9 && seq.times[8] == 9 * tick_us);
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Matrix row "Same glide, new tempo": the same glide submitted again with a
// new tick_len keeps its start and t_end.
void test_ccout_same_glide_new_tempo(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, 125000, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, 250000, 250000, &out, &seq);
  poll_at(&e, 500000, &out, &seq);
  U8 const want[] = {64};
  CHECK(seq_is(&seq, want, 1));
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// Matrix row "Cancel" (and, with ged_set_playing's ccout_cancel, "Pause"):
// cancel ends the glide and keeps the last value and the emission time; the
// next glide starts from the last value sent.
void test_ccout_cancel_keeps_last_value(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, tick_us, &out, &seq);
  ccout_cancel(&e);
  CHECK(is_idle(&e));
  for (U64 k = 2; k <= 5; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  ccout_cancel(&e); // idle: nothing to do
  glide_at(&e, 80, Rate_w, tick_us, 5 * tick_us, &out, &seq);
  poll_at(&e, 6 * tick_us, &out, &seq);
  U8 const want[] = {0, 16, 32}; // 16 + (80 - 16) / 4
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));

  // The emission time is kept too: after a cancel, a new glide's first
  // step is still held back until 10 000 us after the instant.
  ccout_init(&e);
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  ccout_cancel(&e);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, 5000, &out, &seq);
  poll_at(&e, 10000, &out, &seq);
  U8 const want_gap[] = {0, 1};
  CHECK(seq_is(&seq, want_gap, ORCA_ARRAY_COUNTOF(want_gap)));
  CHECK(seq.count == 2 && seq.times[1] == 10000);
  oevent_list_deinit(&out);
}

// Matrix row "Forget": after a forget the next glide jumps to its target,
// and a glide active at the forget continues, under the same emission times.
void test_ccout_forget(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  ccout_forget(&e);
  CHECK(!is_idle(&e));
  poll_at(&e, 5000, &out, &seq); // the instant's time still holds it back
  for (U64 k = 1; k <= 5; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want[] = {0, 16, 32, 48, 64};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(is_idle(&e));

  ccout_forget(&e);
  seq_clear(&seq);
  glide_at(&e, 10, Rate_w, tick_us, 6 * tick_us, &out, &seq);
  CHECK(is_idle(&e));
  for (U64 k = 7; k <= 11; ++k)
    poll_at(&e, k * tick_us, &out, &seq);
  U8 const want_jump[] = {10};
  CHECK(seq_is(&seq, want_jump, 1));
  CHECK(seq.count == 1 && seq.times[0] == 6 * tick_us);
  oevent_list_deinit(&out);
}

// Checks that ccout_next_deadline from now is want, and that it is the first
// change: polls at every time from now to want - 1 emit nothing and end no
// glide, and a poll at want emits or ends one. The polls advance e.
static bool deadline_is_first_change(Ccout_engine *e, U64 now, U64 want,
                                     Oevent_list *out) {
  U64 t;
  if (!ccout_next_deadline(e, now, &t) || t != want)
    return false;
  Usz active = e->active_count;
  for (U64 u = now; u < want; ++u) {
    oevent_list_clear(out);
    ccout_poll(e, u, out);
    if (out->count != 0 || e->active_count != active)
      return false;
  }
  oevent_list_clear(out);
  ccout_poll(e, want, out);
  return out->count > 0 || e->active_count < active;
}

// Matrix row "Deadline": false when idle; otherwise the exact earliest time a
// poll emits or ends a glide. The cases: a held step, then a step that waits
// for the value to move (0 to 2 at rate 28); a retarget from 33 toward 0
// whose path crosses the last value, 32, first while glide_hz holds it back
// and then where it already differs; one that ends at t_end without
// emitting; an overdue glide; and the earliest of two.
void test_ccout_next_deadline(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  U64 t = 0;
  CHECK(!ccout_next_deadline(&e, 0, &t));

  // 0 to 2 at rate 28 (d = 1 000 000): the value reaches 1 at 250 000 and
  // 2 at 750 000, which ends the glide.
  ccout_submit_instant(&e, Ch, Cc, 0, 0, &out);
  ccout_submit_glide(&e, Ch, Cc, 2, 28, tick_us, 0, &out);
  CHECK(deadline_is_first_change(&e, 0, 250000, &out));
  CHECK(deadline_is_first_change(&e, 250001, 750000, &out));
  CHECK(is_idle(&e));
  CHECK(!ccout_next_deadline(&e, 750000, &t));

  // The rounding of each closed-form branch, on inexact and exact
  // quotients. Rising, 0 to 3 at rate 28 (d = 1 000 000): the value reaches
  // 0.5 at 1 000 000 / 6 = 166 666.7 us, so the step is at 166 667.
  enum { T3 = 3000000 };
  ccout_init(&e);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc, 0, T3, &out);
  ccout_submit_glide(&e, Ch, Cc, 3, 28, tick_us, T3, &out);
  CHECK(deadline_is_first_change(&e, T3, T3 + 166667, &out));
  CHECK(out.count == 1 && out.buffer[0].midi_cc.value == 1);
  // Falling, 1 to 0 at w (d = 500 000): the value is exactly 0.5 at
  // 250 000, which rounds up to 1, so the step to 0 is at 250 001.
  ccout_init(&e);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc, 1, T3, &out);
  ccout_submit_glide(&e, Ch, Cc, 0, Rate_w, tick_us, T3, &out);
  CHECK(deadline_is_first_change(&e, T3, T3 + 250001, &out));
  CHECK(out.count == 1 && out.buffer[0].midi_cc.value == 0 && is_idle(&e));
  ccout_init(&e);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc, 1, T3, &out);
  ccout_submit_glide(&e, Ch, Cc, 0, Rate_w, tick_us, T3, &out);
  oevent_list_clear(&out);
  ccout_poll(&e, T3 + 250000, &out);
  CHECK(out.count == 0);
  ccout_poll(&e, T3 + 250001, &out);
  CHECK(out.count == 1 && out.buffer[0].midi_cc.value == 0);

  // From 33 toward 0 at rate 34 (d = 250 000), with 32 the last value sent.
  // The start keeps its fraction, 32.512 (2 130 706 / 65 536), which rounds
  // to 33. glide_hz holds steps until 260 000, where the value rounds to 32,
  // so the next step is 31, where the value falls below 31.5: at
  // 254 000 + 7 782.
  enum { T = 2000000 };
  ccout_init(&e);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc, 0, T, &out);
  ccout_submit_glide(&e, Ch, Cc, 64, Rate_w, tick_us, T, &out);
  ccout_poll(&e, T + 250000, &out); // 32
  ccout_submit_glide(&e, Ch, Cc, 0, 34, tick_us, T + 254000, &out); // from 33
  CHECK(out.count == 2 && out.buffer[1].midi_cc.value == 32);
  CHECK(deadline_is_first_change(&e, T + 254000, T + 261782, &out));
  CHECK(out.count == 1 && out.buffer[0].midi_cc.value == 31);

  // The same retarget at 260 000, where glide_hz no longer holds it: the
  // start, 33, already differs from 32, so the deadline is now.
  ccout_init(&e);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc, 0, T, &out);
  ccout_submit_glide(&e, Ch, Cc, 64, Rate_w, tick_us, T, &out);
  ccout_poll(&e, T + 250000, &out);
  ccout_submit_glide(&e, Ch, Cc, 0, 34, tick_us, T + 260000, &out);
  CHECK(ccout_next_deadline(&e, T + 260000, &t) && t == T + 260000);
  oevent_list_clear(&out);
  ccout_poll(&e, T + 259999, &out); // earlier: glide_hz holds it
  CHECK(out.count == 0);
  ccout_poll(&e, T + 260000, &out);
  CHECK(out.count == 1 && out.buffer[0].midi_cc.value == 33);

  // From 33 back to 32, the last value sent, at glide_hz 5 (steps 200 000
  // us apart): by the time a step may go, the value is 32 again, so the
  // glide ends at t_end without emitting.
  ccout_init(&e);
  ccout_configure(&e, 5);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc, 32, T, &out);
  ccout_submit_glide(&e, Ch, Cc, 40, 34, tick_us, T, &out);
  ccout_submit_glide(&e, Ch, Cc, 32, 34, tick_us, T + 20000, &out); // from 33
  CHECK(out.count == 1 && e.active_count == 1);
  CHECK(deadline_is_first_change(&e, T + 20000, T + 270000, &out));
  CHECK(out.count == 0 && is_idle(&e));

  // Overdue: past t_end, the deadline is now.
  ccout_init(&e);
  ccout_submit_instant(&e, Ch, Cc, 0, 0, &out);
  ccout_submit_glide(&e, Ch, Cc, 64, Rate_w, tick_us, 0, &out);
  CHECK(ccout_next_deadline(&e, 600000, &t) && t == 600000);

  // Two glides: the earliest wins. After a forget neither last value is
  // known, so each glide steps as soon as glide_hz allows: CC 75 at 10 000,
  // 10 000 after its instant, then CC 74 at 15 000.
  ccout_init(&e);
  oevent_list_clear(&out);
  ccout_submit_instant(&e, Ch, Cc + 1, 0, 0, &out);
  ccout_submit_glide(&e, Ch, Cc + 1, 64, Rate_w, tick_us, 0, &out);
  ccout_submit_instant(&e, Ch, Cc, 0, 5000, &out);
  ccout_submit_glide(&e, Ch, Cc, 64, Rate_w, tick_us, 5000, &out);
  ccout_forget(&e);
  CHECK(ccout_next_deadline(&e, 5000, &t) && t == 10000);
  CHECK(deadline_is_first_change(&e, 5000, 10000, &out));
  CHECK(out.count == 1 && out.buffer[0].midi_cc.control == Cc + 1);
  CHECK(deadline_is_first_change(&e, 10001, 15000, &out));
  CHECK(out.count == 1 && out.buffer[0].midi_cc.control == Cc);
  oevent_list_deinit(&out);
}

// Matrix row "Time goes back": after 32 goes out at t0 + 250 000, a poll at
// t0 + 240 000 sends nothing. An unguarded U64 subtraction would wrap past
// the glide_hz gap and send the interpolant there, 31.
void test_ccout_time_goes_back(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  poll_at(&e, 250000, &out, &seq);
  poll_at(&e, 240000, &out, &seq);
  U8 const want[] = {32};
  CHECK(seq_is(&seq, want, 1));
  CHECK(!is_idle(&e));
  oevent_list_deinit(&out);
}

// The worst-case magnitude: a 1 BPM tick (15 000 000 us) at rate 1 (35
// ticks), 127 to 0, polled at the midpoint, where the value is 63.5: 64.
void test_ccout_worst_case_magnitude(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 127, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 0, 1, 15000000, 0, &out, &seq);
  poll_at(&e, (U64)35 * 15000000 / 2, &out, &seq);
  U8 const want[] = {64};
  CHECK(seq_is(&seq, want, 1));
  oevent_list_deinit(&out);
}

// Matrix row "Moving target": a slow glide (rate 1, 35 ticks) whose target
// alternates 72 and 76, submitted after each tick's poll, as the tick path
// does. Each submit is a retarget whose start keeps 16 fractional bits of
// the interpolant, floored, so the value creeps up. A start rounded to a
// whole value froze it at 64.
void test_ccout_moving_target(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 64, 0, &out, &seq);
  seq_clear(&seq);
  for (U64 k = 0; k <= 16; ++k) {
    poll_at(&e, k * tick_us, &out, &seq);
    glide_at(&e, k % 2 == 0 ? 72 : 76, 1, tick_us, k * tick_us, &out, &seq);
  }
  U8 const want[] = {65, 66, 67, 68};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  U64 const want_ticks[] = {2, 6, 10, 16};
  bool ticks_ok = seq.count == 4;
  for (Usz i = 0; ticks_ok && i < 4; ++i)
    ticks_ok = seq.times[i] == want_ticks[i] * tick_us;
  CHECK(ticks_ok);
  oevent_list_deinit(&out);
}

// Submit rule 2 comes before rule 3: after a forget, the same glide
// submitted again sends its target at once and ends the glide.
void test_ccout_rule_unknown_before_same(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, tick_us, 0, &out, &seq);
  ccout_forget(&e);
  glide_at(&e, 64, Rate_w, tick_us, 1000, &out, &seq);
  CHECK(is_idle(&e));
  U8 const want[] = {0, 64};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(seq.count == 2 && seq.times[1] == 1000);
  oevent_list_deinit(&out);
}

// Submit rule 3 comes before rule 4: at glide_hz 1 the steps of 0 to 1 at
// rate 34 are held back, and at 200 000 the interpolant (0.8) rounds to the
// target. The same glide submitted then keeps running, and sends 1 at
// t_end; rule 4 would have cancelled it with nothing sent.
void test_ccout_rule_same_before_noop(void) {
  Ccout_engine e;
  ccout_init(&e);
  ccout_configure(&e, 1);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 1, 34, tick_us, 0, &out, &seq);
  poll_at(&e, 200000, &out, &seq);
  glide_at(&e, 1, 34, tick_us, 200000, &out, &seq);
  CHECK(!is_idle(&e));
  poll_at(&e, 249999, &out, &seq);
  poll_at(&e, 250000, &out, &seq);
  U8 const want[] = {0, 1};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(seq.count == 2 && seq.times[1] == 250000);
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// tick_len's guards. 0 counts as 1, so a glide at w lasts 4 us, and a
// retarget at the same time divides by that, never by 0. UINT64_MAX counts
// as 2^32, so a glide at w lasts 2^34 us, halfway at 2^33.
void test_ccout_tick_len_guards(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, 0, 1000, &out, &seq);
  glide_at(&e, 80, Rate_w, 0, 1000, &out, &seq); // a retarget, from 0
  U64 t = 0;
  CHECK(ccout_next_deadline(&e, 1000, &t) && t == 1004);
  poll_at(&e, 1003, &out, &seq); // glide_hz holds the step back
  poll_at(&e, 1004, &out, &seq); // t_end: the target
  U8 const want[] = {0, 80};
  CHECK(seq_is(&seq, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(is_idle(&e));

  U64 const max_d = (U64)4 << 32; // 4 ticks of 2^32 us
  ccout_init(&e);
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  glide_at(&e, 64, Rate_w, UINT64_MAX, 0, &out, &seq);
  CHECK(ccout_next_deadline(&e, max_d - 1, &t) && t == max_d - 1);
  poll_at(&e, max_d / 2, &out, &seq);
  poll_at(&e, max_d, &out, &seq);
  U8 const want_max[] = {0, 32, 64};
  CHECK(seq_is(&seq, want_max, ORCA_ARRAY_COUNTOF(want_max)));
  CHECK(is_idle(&e));
  oevent_list_deinit(&out);
}

// A poll before a glide's start time (time went back past the submit) sees
// the glide at its start: it sends nothing, and the glide stays active.
void test_ccout_poll_before_start(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  Seq seq;
  seq_clear(&seq);
  instant_at(&e, 0, 0, &out, &seq);
  seq_clear(&seq);
  glide_at(&e, 64, Rate_w, tick_us, 500000, &out, &seq);
  poll_at(&e, 400000, &out, &seq);
  CHECK(seq.count == 0 && !seq.bad);
  CHECK(e.active_count == 1);
  oevent_list_deinit(&out);
}

// A poll emits in slot order, channel * 128 + cc, whatever the order of the
// submits.
void test_ccout_poll_slot_order(void) {
  Ccout_engine e;
  ccout_init(&e);
  Oevent_list out;
  oevent_list_init(&out);
  ccout_submit_instant(&e, 1, 0, 0, 0, &out);
  ccout_submit_instant(&e, 0, 5, 0, 0, &out);
  ccout_submit_instant(&e, 15, 127, 0, 0, &out);
  ccout_submit_glide(&e, 15, 127, 64, Rate_w, tick_us, 0, &out);
  ccout_submit_glide(&e, 1, 0, 64, Rate_w, tick_us, 0, &out);
  ccout_submit_glide(&e, 0, 5, 64, Rate_w, tick_us, 0, &out);
  CHECK(e.active_count == 3);
  oevent_list_clear(&out);
  ccout_poll(&e, tick_us, &out);
  CHECK(out.count == 3);
  if (out.count == 3) {
    CHECK(out.buffer[0].midi_cc.channel == 0 &&
          out.buffer[0].midi_cc.control == 5);
    CHECK(out.buffer[1].midi_cc.channel == 1 &&
          out.buffer[1].midi_cc.control == 0);
    CHECK(out.buffer[2].midi_cc.channel == 15 &&
          out.buffer[2].midi_cc.control == 127);
    for (Usz i = 0; i < 3; ++i)
      CHECK(out.buffer[i].any.oevent_type == Oevent_type_midi_cc &&
            out.buffer[i].midi_cc.value == 16);
  }
  oevent_list_deinit(&out);
}

// ccout_init sets every field, whatever the memory held: no glide, every
// last value unknown, no emission times, glide_hz 100. ccout_configure
// rounds the step gap up to whole microseconds.
void test_ccout_init_and_configure(void) {
  Ccout_engine e;
  memset(&e, 0xA5, sizeof e);
  ccout_init(&e);
  CHECK(e.active_count == 0 && e.glide_hz == 100 && e.step_gap == 10000);
  bool idle = true, unknown = true, never = true;
  for (Usz i = 0; i < Ccout_slots; ++i) {
    if (e.glides[i].active)
      idle = false;
    if (e.last_value[i / Ccout_controls][i % Ccout_controls] != -1)
      unknown = false;
    if (e.last_emit[i] != UINT64_MAX)
      never = false;
  }
  CHECK(idle && unknown && never);
  ccout_configure(&e, 200);
  CHECK(e.glide_hz == 200 && e.step_gap == 5000);
  ccout_configure(&e, 300);
  CHECK(e.step_gap == 3334); // 3 333.3 us, rounded up
  ccout_configure(&e, 2000000);
  CHECK(e.step_gap == 1);
  ccout_configure(&e, 0);
  CHECK(e.glide_hz == 100 && e.step_gap == 10000);

  // With no emission time, a glide's first step is not held back. The test
  // sets a last value directly, so the controller has one but no emission.
  Oevent_list out;
  oevent_list_init(&out);
  ccout_init(&e);
  e.last_value[Ch][Cc] = 0;
  ccout_submit_glide(&e, Ch, Cc, 64, Rate_w, tick_us, 0, &out);
  oevent_list_clear(&out);
  ccout_poll(&e, 1000, &out); // 0.128, rounded: 0
  ccout_poll(&e, 4000, &out); // 0.512: 1
  CHECK(out.count == 1 && out.buffer[0].midi_cc.value == 1);
  oevent_list_deinit(&out);
}
