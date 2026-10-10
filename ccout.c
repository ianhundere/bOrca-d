// The CC engine (architecture spine AD-9, AD-10). See ccout.h.
#include "ccout.h"
#include "base.h"
#include "vmio.h"

// last_emit's value for a controller that has emitted nothing.
static U64 const never_emitted = UINT64_MAX;

// tick_len's ceiling, about 71.6 minutes per tick: far beyond any tempo,
// and it keeps every product below in range. d is then below 2^37.2, and a
// fixed-point value or difference below 2^23, so their products stay below
// 2^61.
static U64 const tick_len_max = (U64)1 << 32;

// 1.0 in a glide's fixed-point values.
static I64 const fixed_one = (I64)1 << Ccout_frac_bits;

static Usz slot_of(U8 ch, U8 cc) { return (Usz)ch * Ccout_controls + cc; }

static I8 *last_value_of(Ccout_engine *e, Usz slot) {
  return &e->last_value[slot / Ccout_controls][slot % Ccout_controls];
}

static I8 last_value_at(Ccout_engine const *e, Usz slot) {
  return e->last_value[slot / Ccout_controls][slot % Ccout_controls];
}

// floor(n / d) for d > 0. C99 division truncates toward zero.
static I64 floor_div(I64 n, I64 d) {
  I64 q = n / d;
  if (n % d != 0 && n < 0)
    --q;
  return q;
}

// The glide's value at now, with Ccout_frac_bits fractional bits:
// start + floor((target - start) * el / d), el = now - t0 in [0, d]. It lies
// between the start and the target, so it is never negative.
static I64 glide_fixed(Ccout_glide const *g, U64 now) {
  U64 d = g->t_end - g->t0;
  U64 el = now > g->t0 ? now - g->t0 : 0;
  if (el > d)
    el = d;
  I64 delta = (I64)g->target * fixed_one - (I64)g->start;
  return (I64)g->start + floor_div(delta * (I64)el, (I64)d);
}

// A fixed-point value rounded half up. Rounding glide_fixed's floored value
// equals rounding the exact interpolant from the stored start, because every
// half is representable.
static U8 rounded(I64 fixed) {
  return (U8)((fixed + fixed_one / 2) / fixed_one);
}

static U8 glide_value(Ccout_glide const *g, U64 now) {
  return rounded(glide_fixed(g, now));
}

static void end_glide(Ccout_engine *e, Usz slot) {
  assert(e->glides[slot].active && e->active_count > 0);
  e->glides[slot].active = false;
  --e->active_count;
}

// The budget's one check-and-debit point (AD-10). A step needs a token and is
// held back without one; instants and target emissions always debit. It never
// limits in B3: I2 fills it with the token bucket.
static bool budget_take(Ccout_engine *e, U64 now, bool is_step) {
  (void)e;
  (void)now;
  (void)is_step;
  return true;
}

// Every emission goes through here. False only for a step the budget holds
// back.
static bool emit(Ccout_engine *e, Usz slot, U8 value, U64 now, bool is_step,
                 Oevent_list *out) {
  if (!budget_take(e, now, is_step))
    return false;
  Oevent_midi_cc *oe = &oevent_list_alloc_item(out)->midi_cc;
  oe->oevent_type = Oevent_type_midi_cc;
  oe->channel = (U8)(slot / Ccout_controls);
  oe->control = (U8)(slot % Ccout_controls);
  oe->value = value;
  *last_value_of(e, slot) = (I8)value;
  e->last_emit[slot] = now;
  return true;
}

void ccout_init(Ccout_engine *engine) {
  memset(engine->glides, 0, sizeof engine->glides);
  for (Usz i = 0; i < Ccout_slots; ++i)
    engine->last_emit[i] = never_emitted;
  memset(engine->last_value, 0xFF, sizeof engine->last_value); // all -1
  engine->active_count = 0;
  ccout_configure(engine, 0);
}

void ccout_configure(Ccout_engine *engine, Usz glide_hz) {
  if (glide_hz == 0)
    glide_hz = Ccout_glide_hz_default;
  engine->glide_hz = glide_hz;
  // Steps need now - last emission >= 1 000 000 / glide_hz; for whole
  // microseconds that is >= the ceiling.
  U64 hz = (U64)glide_hz;
  engine->step_gap = hz >= 1000000 ? 1 : (1000000 + hz - 1) / hz;
}

void ccout_submit_instant(Ccout_engine *engine, U8 ch, U8 cc, U8 value, U64 now,
                          Oevent_list *out) {
  assert(ch < Ccout_channels && cc < Ccout_controls && value <= 127);
  Usz slot = slot_of(ch, cc);
  if (engine->glides[slot].active)
    end_glide(engine, slot);
  emit(engine, slot, value, now, false, out);
}

void ccout_submit_glide(Ccout_engine *engine, U8 ch, U8 cc, U8 target,
                        U8 rate_idx, U64 tick_len, U64 now, Oevent_list *out) {
  assert(ch < Ccout_channels && cc < Ccout_controls && target <= 127 &&
         rate_idx <= 35);
  // 1. Rates 0 and 35 are instant, judged by the index, not the glyph.
  // 2. With the last value unknown, the target goes out at once.
  Usz slot = slot_of(ch, cc);
  I8 last = last_value_at(engine, slot);
  if (rate_idx == 0 || rate_idx == 35 || last < 0) {
    ccout_submit_instant(engine, ch, cc, target, now, out);
    return;
  }
  Ccout_glide *g = &engine->glides[slot];
  // 3. The same glide again keeps running, with its start and t_end.
  if (g->active && g->target == target && g->rate == rate_idx)
    return;
  // 4. A target equal to the current value, rounded: cancel, send nothing.
  I64 current = g->active ? glide_fixed(g, now) : (I64)last * fixed_one;
  if (target == rounded(current)) {
    if (g->active)
      end_glide(engine, slot);
    return;
  }
  // 5. A new glide, or a retarget, from the current value.
  if (tick_len == 0)
    tick_len = 1;
  else if (tick_len > tick_len_max)
    tick_len = tick_len_max;
  g->t0 = now;
  g->t_end = now + (U64)(36 - rate_idx) * tick_len;
  g->start = (U32)current; // the interpolant, floored to 16 fraction bits
  g->target = target;
  g->rate = rate_idx;
  if (!g->active) {
    g->active = true;
    ++engine->active_count;
  }
}

// One active glide's part of a poll.
static void poll_slot(Ccout_engine *e, Usz slot, U64 now, Oevent_list *out) {
  Ccout_glide *g = &e->glides[slot];
  I8 last = last_value_at(e, slot);
  if (now >= g->t_end) {
    // The target, unless it is already the last value; never held back.
    if (last != (I8)g->target)
      emit(e, slot, g->target, now, false, out);
    end_glide(e, slot);
    return;
  }
  U64 last_emit = e->last_emit[slot];
  if (last_emit != never_emitted &&
      (now < last_emit || now - last_emit < e->step_gap))
    return; // too soon after the last emission, or time went back
  U8 value = glide_value(g, now);
  if (last >= 0 && value == (U8)last)
    return; // de-duplicated
  if (!emit(e, slot, value, now, true, out))
    return;
  if (value == g->target)
    end_glide(e, slot);
}

void ccout_poll(Ccout_engine *engine, U64 now_us, Oevent_list *out) {
  Usz left = engine->active_count;
  for (Usz slot = 0; left > 0 && slot < Ccout_slots; ++slot) {
    if (!engine->glides[slot].active)
      continue;
    --left;
    poll_slot(engine, slot, now_us, out);
  }
}

// The earliest time, never before now, at which a poll emits on or ends the
// active glide in slot.
static U64 slot_deadline(Ccout_engine const *e, Usz slot, U64 now) {
  Ccout_glide const *g = &e->glides[slot];
  if (now >= g->t_end)
    return now;
  // The first time glide_hz allows a step.
  U64 t = now;
  U64 last_emit = e->last_emit[slot];
  if (last_emit != never_emitted) {
    U64 gate = last_emit + e->step_gap;
    if (gate < last_emit)
      gate = UINT64_MAX;
    if (t < gate)
      t = gate;
  }
  if (t >= g->t_end)
    return g->t_end;
  I8 last = last_value_at(e, slot);
  if (last < 0 || glide_value(g, t) != (U8)last)
    return t;
  // The value at t rounds to the last value, so the fixed-point value x lies
  // in [b_low, b_high), b_low = (last - 1/2) * one, b_high = (last + 1/2) *
  // one; x is monotonic from start s, so the next step is where it first
  // leaves that range. With d, and A = |target * one - s| (never 0: rule 4,
  // s itself never rounds to the target):
  //   rising, x = s + floor(A * el / d), to x >= b_high:
  //     el >= (b_high - s) * d / A, rounded up;
  //   falling, x = s - ceil(A * el / d), to x < b_low:
  //     el > (s - b_low) * d / A, the floor plus 1.
  // x(t) lies in the range and between s and the target, so b_high > s when
  // rising and s >= b_low when falling.
  U64 d = g->t_end - g->t0;
  I64 s = (I64)g->start;
  I64 end = (I64)g->target * fixed_one;
  I64 half = fixed_one / 2;
  U64 el;
  if (end > s) {
    U64 num = (U64)((2 * (I64)last + 1) * half - s) * d;
    U64 den = (U64)(end - s);
    el = (num + den - 1) / den;
  } else {
    U64 num = (U64)(s - (2 * (I64)last - 1) * half) * d;
    U64 den = (U64)(s - end);
    el = num / den + 1;
  }
  U64 step = g->t0 + el;
  return step < g->t_end ? step : g->t_end;
}

bool ccout_next_deadline(Ccout_engine const *engine, U64 now, U64 *t) {
  if (engine->active_count == 0)
    return false;
  U64 best = UINT64_MAX;
  Usz left = engine->active_count;
  for (Usz slot = 0; left > 0 && slot < Ccout_slots; ++slot) {
    if (!engine->glides[slot].active)
      continue;
    --left;
    U64 when = slot_deadline(engine, slot, now);
    if (when < best)
      best = when;
  }
  *t = best;
  return true;
}

void ccout_forget(Ccout_engine *engine) {
  memset(engine->last_value, 0xFF, sizeof engine->last_value); // all -1
}

void ccout_cancel(Ccout_engine *engine) {
  if (engine->active_count == 0)
    return;
  for (Usz slot = 0; slot < Ccout_slots; ++slot)
    engine->glides[slot].active = false;
  engine->active_count = 0;
}
