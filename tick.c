// The tick body, the sustained-note list and the event send (architecture
// spine AD-12, AD-14). See tick.h.
#include "tick.h"
#include "base.h"
#include "ccout.h"
#include "gbuffer.h"
#include "opstate.h"
#include "sim.h"
#include "vmio.h"

void susnote_list_init(Susnote_list *sl) {
  sl->buffer = NULL;
  sl->count = 0;
  sl->capacity = 0;
}

void susnote_list_deinit(Susnote_list *sl) { free(sl->buffer); }

void susnote_list_clear(Susnote_list *sl) { sl->count = 0; }

bool susnote_list_add_notes(Susnote_list *sl, Susnote const *restrict notes,
                            Usz added_count, Usz *restrict start_removed,
                            Usz *restrict end_removed) {
  Susnote *buffer = sl->buffer;
  Usz count = sl->count;
  Usz cap = sl->capacity;
  Usz rem = count + added_count;
  Usz needed_cap = rem + added_count;
  if (cap < needed_cap) {
    cap = needed_cap < 16 ? 16 : orca_round_up_power2(needed_cap);
    buffer = realloc(buffer, cap * sizeof(Susnote));
    if (!buffer)
      return false; // the old buffer, and so the list, is unchanged
    sl->capacity = cap;
    sl->buffer = buffer;
  }
  *start_removed = rem;
  Usz i_in = 0;
  for (; i_in < added_count; ++i_in) {
    Susnote this_in = notes[i_in];
    for (Usz i_old = 0; i_old < count; ++i_old) {
      Susnote this_old = buffer[i_old];
      if (this_old.chan_note == this_in.chan_note) {
        buffer[i_old] = this_in;
        buffer[rem] = this_old;
        ++rem;
        goto next_in;
      }
    }
    buffer[count] = this_in;
    ++count;
  next_in:;
  }
  sl->count = count;
  *end_removed = rem;
  return true;
}

void susnote_list_advance_tick(Susnote_list *sl, Usz *restrict start_removed,
                               Usz *restrict end_removed) {
  Susnote *restrict buffer = sl->buffer;
  Usz count = sl->count;
  *end_removed = count;
  // A released note swaps with the last unvisited one, so the note-offs of
  // notes released together go out in that swap order: for three notes
  // that are 1st, 2nd and 3rd in the list, the 2nd, the 3rd, then the 1st.
  for (Usz i = 0; i < count;) {
    Susnote sn = buffer[i];
    if (sn.remaining > 1) {
      --buffer[i].remaining;
      ++i;
    } else {
      --count;
      buffer[i] = buffer[count];
      buffer[count] = sn;
    }
  }
  *start_removed = count;
  sl->count = count;
}

void susnote_list_remove_by_chan_mask(Susnote_list *sl, Usz chan_mask,
                                      Usz *restrict start_removed,
                                      Usz *restrict end_removed) {
  Susnote *restrict buffer = sl->buffer;
  Usz count = sl->count;
  *end_removed = count;
  for (Usz i = 0; i < count;) {
    Susnote sn = buffer[i];
    Usz chan = sn.chan_note >> 8;
    if (chan_mask & 1u << chan) {
      --count;
      buffer[i] = buffer[count];
      buffer[count] = sn;
    } else {
      ++i;
    }
  }
  *start_removed = count;
  sl->count = count;
}

static void send_chan_msg(Tick_sink const *sink, int type /*0..15*/,
                          int chan /*0.. 15*/, int byte1 /*0..127*/,
                          int byte2 /*0..127*/) {
  sink->midi3(sink->u, type << 4 | chan, byte1, byte2);
}

static void send_note_offs(Tick_sink const *sink, Susnote const *start,
                           Susnote const *end) {
  for (; start != end; ++start) {
    U16 chan_note = start->chan_note;
    send_chan_msg(sink, 0x8, chan_note >> 8, chan_note & 0xFF, 0);
  }
}

// Step 2 of the wire order in tick.h. With nothing sustained it sends
// nothing and touches no buffer, which may be NULL.
static void age_sustained_notes(Tick_sink const *sink,
                                Susnote_list *susnotes) {
  if (susnotes->count == 0)
    return;
  Usz start_removed, end_removed;
  susnote_list_advance_tick(susnotes, &start_removed, &end_removed);
  if (ORCA_UNLIKELY(start_removed != end_removed)) {
    Susnote const *restrict susnotes_off = susnotes->buffer;
    send_note_offs(sink, susnotes_off + start_removed,
                   susnotes_off + end_removed);
  }
}

void tick_release_all(Tick_sink const *sink, Susnote_list *susnotes) {
  if (susnotes->count == 0)
    return; // the buffer may be NULL, and NULL + 0 is undefined
  send_note_offs(sink, susnotes->buffer, susnotes->buffer + susnotes->count);
  susnote_list_clear(susnotes);
}

// Sends the CC engine's output, which holds only Oevent_midi_cc items: step
// 3 of the wire order in tick.h, and each submit's output in step 4. The
// engine list never goes through tick_send_events, which would submit its
// CCs to the engine again.
static void send_engine_ccs(Tick_sink const *sink, Oevent_list const *engine) {
  for (Usz i = 0; i < engine->count; ++i) {
    Oevent_midi_cc const *ec = &engine->buffer[i].midi_cc;
    send_chan_msg(sink, 0xb, ec->channel, ec->control, ec->value);
  }
}

// Sends the VM's events: steps 4 to 6 of the wire order in tick.h.
static void tick_send_events(Tick_ctx const *ctx, Tick_sink const *sink,
                             Oevent const *events, Usz count) {
  Susnote_list *susnotes = ctx->susnotes;
  Ccout_engine *ccout = ctx->ccout;
  Oevent_list *engine = ctx->engine_list;
  enum { Midi_on_capacity = 512 };
  typedef struct {
    U8 channel;
    U8 note_number;
    U8 velocity;
  } Midi_note_on;
  typedef struct {
    U8 note_number;
    U8 velocity;
    U8 duration;
  } Midi_mono_on;
  Midi_note_on midi_note_ons[Midi_on_capacity];
  Midi_mono_on midi_mono_ons[16]; // Keep only a single one per channel
  Susnote new_susnotes[Midi_on_capacity];
  Usz midi_note_count = 0;
  Usz monofied_chans = 0; // bitset of channels with new mono notes

  for (Usz i = 0; i < count; ++i) {
    Oevent const *e = events + i;
    switch ((Oevent_types)e->any.oevent_type) {
    case Oevent_type_midi_note: {
      if (midi_note_count == Midi_on_capacity)
        break;
      Oevent_midi_note const *em = &e->midi_note;
      Usz note_number = (Usz)(12u * em->octave + em->note);
      if (note_number > 127)
        note_number = 127;
      Usz channel = em->channel;
      if (channel > 15)
        break;
      if (em->mono) {
        // 'mono' note-ons are strange. The more typical branch you'd expect to
        // see, where you can play multiple notes per channel, is below.
        monofied_chans |= 1u << (channel & 0xFu);
        midi_mono_ons[channel] = (Midi_mono_on){.note_number = (U8)note_number,
                                                .velocity = em->velocity,
                                                .duration = em->duration};
      } else {
        midi_note_ons[midi_note_count] =
            (Midi_note_on){.channel = (U8)channel,
                           .note_number = (U8)note_number,
                           .velocity = em->velocity};
        new_susnotes[midi_note_count] =
            (Susnote){.remaining = em->duration,
                      .chan_note = (U16)((channel << 8u) | note_number)};
        ++midi_note_count;
      }
      break;
    }
    case Oevent_type_midi_cc: {
      Oevent_midi_cc const *ec = &e->midi_cc;
      // Step 4 (tick.h): CCs, pitch bends and OSC/UDP go out at once, in list
      // order; notes wait for steps 5 and 6. A CC is an instant: it goes
      // through the engine, which always emits it, and out at once.
      oevent_list_clear(engine);
      ccout_submit_instant(ccout, ec->channel, ec->control, ec->value,
                           ctx->now_us, engine);
      send_engine_ccs(sink, engine);
      break;
    }
    case Oevent_type_midi_cc_interpolated: {
      Oevent_midi_cc_interpolated const *eci = &e->midi_cc_interpolated;
      // Whatever the engine emits for the submit (an instant rate, or a
      // controller with no last value) goes out at once, in list order; a
      // glide's steps go out in step 3 of later ticks.
      oevent_list_clear(engine);
      ccout_submit_glide(ccout, eci->channel, eci->control, eci->target_value,
                         eci->interpolation_rate, ctx->tick_len, ctx->now_us,
                         engine);
      send_engine_ccs(sink, engine);
      break;
    }
    case Oevent_type_midi_pb: {
      Oevent_midi_pb const *ep = &e->midi_pb;
      send_chan_msg(sink, 0xe, ep->channel, ep->lsb, ep->msb);
      break;
    }
    case Oevent_type_osc_ints:
    case Oevent_type_udp_string:
      sink->osc(sink->u, e);
      break;
    }
  }

  // Step 5 (tick.h), and again for the mono note-ons of step 6. If the list
  // cannot grow, this batch is skipped: none of its note-offs or note-ons go
  // out, so no note sounds without a sustained entry to release it.
do_note_ons:
  if (midi_note_count > 0) {
    Usz start_note_offs, end_note_offs;
    if (susnote_list_add_notes(susnotes, new_susnotes, midi_note_count,
                               &start_note_offs, &end_note_offs)) {
      if (start_note_offs != end_note_offs) {
        Susnote const *restrict susnotes_off = susnotes->buffer;
        send_note_offs(sink, susnotes_off + start_note_offs,
                       susnotes_off + end_note_offs);
      }
      for (Usz i = 0; i < midi_note_count; ++i) {
        Midi_note_on mno = midi_note_ons[i];
        send_chan_msg(sink, 0x9, mno.channel, mno.note_number, mno.velocity);
      }
    }
  }
  if (monofied_chans) {
    // Step 6 (tick.h): the note-offs of every sustained note on a channel
    // with a new mono note, including a note that step 5 just started there,
    // then the mono note-ons through step 5's path.
    Usz start_note_offs, end_note_offs;
    susnote_list_remove_by_chan_mask(susnotes, monofied_chans,
                                     &start_note_offs, &end_note_offs);
    if (start_note_offs != end_note_offs) {
      Susnote const *restrict susnotes_off = susnotes->buffer;
      send_note_offs(sink, susnotes_off + start_note_offs,
                     susnotes_off + end_note_offs);
    }
    midi_note_count = 0; // We're going to use this list again. Reset it.
    for (Usz i = 0; i < 16; i++) { // Add these notes to list of note-ons
      if (!(monofied_chans & 1u << i))
        continue;
      midi_note_ons[midi_note_count] =
          (Midi_note_on){.channel = (U8)i,
                         .note_number = midi_mono_ons[i].note_number,
                         .velocity = midi_mono_ons[i].velocity};
      new_susnotes[midi_note_count] = (Susnote){
          .remaining = midi_mono_ons[i].duration,
          .chan_note = (U16)((i << 8u) | midi_mono_ons[i].note_number)};
      midi_note_count++;
    }
    monofied_chans = 0;
    goto do_note_ons;
  }
}

void tick_run_vm(Glyph *restrict gbuffer, Mark *restrict mbuffer, Usz height,
                 Usz width, Usz tick_num, Oevent_list *list, Usz random_seed,
                 Opstate_store *opstate) {
  mbuffer_clear(mbuffer, height, width);
  oevent_list_clear(list);
  Orca_run_ctx const ctx = {.opstate = opstate};
  orca_run(gbuffer, mbuffer, height, width, tick_num, list, random_seed, &ctx);
}

void tick_body(Tick_ctx const *ctx, Tick_sink const *sink) {
  assert(ctx->ccout);
  // Steps 2 to 6 of the wire order in tick.h.
  age_sustained_notes(sink, ctx->susnotes);
  // Step 3. The engine appends, so its list is cleared first: nothing left
  // in it, such as a preview's events, is ever sent (CAP-9).
  Oevent_list *engine = ctx->engine_list;
  oevent_list_clear(engine);
  ccout_poll(ctx->ccout, ctx->now_us, engine);
  send_engine_ccs(sink, engine);
  Oevent_list *list = ctx->tick_list;
  tick_run_vm(ctx->gbuffer, ctx->mbuffer, ctx->height, ctx->width,
              *ctx->tick_num, list, ctx->random_seed, ctx->opstate);
  ++*ctx->tick_num;
  if (list->count > 0)
    tick_send_events(ctx, sink, list->buffer, list->count);
}

U64 tick_len_us(Usz bpm) {
  U64 b = bpm > 0 ? (U64)bpm : 1;
  U64 len = (15000000 + b / 2) / b;
  return len > 0 ? len : 1;
}
