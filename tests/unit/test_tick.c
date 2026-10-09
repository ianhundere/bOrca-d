// Unit tests for tick.c (spec items B4 and B8, spine AD-12 to AD-14): the
// tick body on a recording sink, the CAP-9 resume test, the wire order within
// a tick, the glide engine's place in it, release-all, the note lengths in
// tick bodies (B8), tick_run_vm and tick_len_us. Step numbers refer to the
// wire order in tick.h.
//
// sim.c's glide table is global until B3, so a glide left active would leak
// into later runs. Only tick_glide_engine_order uses a CCI, and it drains the
// table before it returns; every other test checks after each tick body that
// the engine sent nothing, so a leaked glide fails with a clear cause.
#include "../../gbuffer.h"
#include "../../opstate.h"
#include "../../sim.h"
#include "../../tick.h"
#include "../../vmio.h"
#include "tests.h"

enum { Rec_capacity = 64 };

typedef struct {
  int status, d1, d2;
} Triple;

// A sink that records every call, in order. Each MIDI triple is stamped with
// body, which the test sets before each tick body, and with the number of
// midi1 calls (the F8s, in the note-length tests) made before it.
typedef struct {
  Triple midi3[Rec_capacity];
  Usz midi3_body[Rec_capacity];
  Usz midi3_pulses[Rec_capacity];
  Usz midi3_count;
  Usz midi1_count;
  Usz osc_count;
  Usz body;
  bool overflow;
} Recording;

static void rec_midi3(void *u, int status, int d1, int d2) {
  Recording *r = u;
  if (r->midi3_count == Rec_capacity) {
    r->overflow = true;
    return;
  }
  r->midi3_body[r->midi3_count] = r->body;
  r->midi3_pulses[r->midi3_count] = r->midi1_count;
  r->midi3[r->midi3_count++] = (Triple){status, d1, d2};
}

static void rec_midi1(void *u, int byte) {
  Recording *r = u;
  (void)byte;
  ++r->midi1_count;
}

static void rec_osc(void *u, Oevent const *e) {
  Recording *r = u;
  (void)e;
  ++r->osc_count;
}

// Empties r and returns a sink that records into it.
static Tick_sink rec_sink(Recording *r) {
  memset(r, 0, sizeof *r);
  return (Tick_sink){
      .u = r, .midi3 = rec_midi3, .midi1 = rec_midi1, .osc = rec_osc};
}

static int triple_cmp(Triple a, Triple b) {
  if (a.status != b.status)
    return a.status < b.status ? -1 : 1;
  if (a.d1 != b.d1)
    return a.d1 < b.d1 ? -1 : 1;
  if (a.d2 != b.d2)
    return a.d2 < b.d2 ? -1 : 1;
  return 0;
}

// Sorts the recorded MIDI triples, so two runs compare as multisets.
static void sort_triples(Recording *r) {
  for (Usz i = 1; i < r->midi3_count; ++i) {
    Triple t = r->midi3[i];
    Usz j = i;
    for (; j > 0 && triple_cmp(r->midi3[j - 1], t) > 0; --j)
      r->midi3[j] = r->midi3[j - 1];
    r->midi3[j] = t;
  }
}

// The recording holds exactly these triples, in this order.
static bool triples_are(Recording const *r, Triple const *want, Usz count) {
  if (r->overflow || r->midi3_count != count)
    return false;
  for (Usz i = 0; i < count; ++i) {
    if (triple_cmp(r->midi3[i], want[i]) != 0)
      return false;
  }
  return true;
}

static bool recordings_equal(Recording const *a, Recording const *b) {
  return !a->overflow && !b->overflow && a->midi1_count == b->midi1_count &&
         a->osc_count == b->osc_count &&
         triples_are(a, b->midi3, b->midi3_count);
}

// Writes text into the grid at (y, x).
static void put(Glyph *grid, Usz width, Usz y, Usz x, char const *text) {
  memcpy(grid + y * width + x, text, strlen(text));
}

// The CAP-9 patch: a note (ch 0, note 36), an instant CC (ch 0, CC 74, 64)
// and a pitch bend (ch 0, LSB 127, MSB 61), each banged on every tick by the
// D two rows above it, and a fourth D with nothing under it, where the edit
// puts a second note (ch 1, note 40).
enum { Cap9_h = 3, Cap9_w = 32, Cap9_edit_x = 24 };

static void cap9_patch(Glyph *grid) {
  memset(grid, '.', Cap9_h * Cap9_w);
  put(grid, Cap9_w, 0, 0, "D1");
  put(grid, Cap9_w, 2, 0, ":03C.1");
  put(grid, Cap9_w, 0, 8, "D1");
  put(grid, Cap9_w, 2, 8, "!0.74g.");
  put(grid, Cap9_w, 0, 17, "D1");
  put(grid, Cap9_w, 2, 17, "?0hz");
  put(grid, Cap9_w, 0, Cap9_edit_x, "D1");
}

// The CAP-9 sequence, as the TUI runs it around a pause:
//   1. a tick body;
//   2. release all (pause);
//   3. the edit, if edit is set: a note operator under the fourth D;
//   4. the preview, if preview is set: the VM on a scratch copy of the grid
//      and the store, into the list that the next tick body gets as its
//      engine list, as 4f349cd shared one list between the two;
//   5. the first tick body after resume, recorded into out.
// *preview_count is the number of events the preview produced.
static void run_cap9(bool edit, bool preview, Recording *out,
                     Usz *preview_count) {
  enum { H = Cap9_h, W = Cap9_w };
  Glyph grid[H * W], scratch[H * W];
  Mark marks[H * W];
  cap9_patch(grid);
  Opstate_store store, scratch_store;
  opstate_init(&store);
  opstate_init(&scratch_store);
  Oevent_list tick_list, engine_list;
  oevent_list_init(&tick_list);
  oevent_list_init(&engine_list);
  Susnote_list susnotes;
  susnote_list_init(&susnotes);
  Usz tick_num = 0;
  Tick_ctx const ctx = {.gbuffer = grid,
                        .mbuffer = marks,
                        .height = H,
                        .width = W,
                        .random_seed = 0,
                        .opstate = &store,
                        .tick_num = &tick_num,
                        .tick_list = &tick_list,
                        .engine_list = &engine_list,
                        .susnotes = &susnotes};

  // 1 and 2: the tick sends the note-on, and pausing releases it.
  Recording before;
  Tick_sink const before_sink = rec_sink(&before);
  tick_body(&ctx, &before_sink);
  CHECK(engine_list.count == 0);
  CHECK(tick_num == 1);
  CHECK(susnotes.count == 1);
  tick_release_all(&before_sink, &susnotes);
  CHECK(susnotes.count == 0);
  Triple const tick0[] = {
      {0xB0, 74, 64}, {0xE0, 127, 61}, {0x90, 36, 127}, {0x80, 36, 0}};
  CHECK(triples_are(&before, tick0, ORCA_ARRAY_COUNTOF(tick0)));

  // 3. The edit.
  if (edit)
    put(grid, W, 2, Cap9_edit_x, ":13E.2");

  // 4. The preview, into the engine list.
  *preview_count = 0;
  if (preview) {
    memcpy(scratch, grid, sizeof scratch);
    CHECK(opstate_copy(&store, &scratch_store));
    tick_run_vm(scratch, marks, H, W, tick_num, &engine_list, 0,
                &scratch_store);
    *preview_count = engine_list.count;
  }

  // 5. The first tick after resume.
  Tick_sink const sink = rec_sink(out);
  tick_body(&ctx, &sink);
  CHECK(engine_list.count == 0); // cleared, and no glide to run
  CHECK(tick_num == 2);

  susnote_list_deinit(&susnotes);
  oevent_list_deinit(&tick_list);
  oevent_list_deinit(&engine_list);
  opstate_free(&store);
  opstate_free(&scratch_store);
}

// The CAP-9 test: tick, pause, an edit that adds a note operator, a preview
// that shares the engine list, then resume. The first tick's MIDI triples
// equal those of a control run that had no preview. A tick body that did not
// clear its engine list would also send the preview's events, retriggering
// the notes and doubling the CC and the pitch bend.
void test_tick_cap9_resume_after_edit(void) {
  Recording resumed, control;
  Usz preview_count, control_preview_count;
  run_cap9(true, true, &resumed, &preview_count);
  run_cap9(true, false, &control, &control_preview_count);
  CHECK(preview_count == 4); // the preview produced events: 2 notes, CC, PB
  CHECK(control_preview_count == 0);
  sort_triples(&resumed);
  sort_triples(&control);
  CHECK(recordings_equal(&resumed, &control));
  Triple const want[] = {
      {0x90, 36, 127}, {0x91, 40, 127}, {0xB0, 74, 64}, {0xE0, 127, 61}};
  CHECK(triples_are(&control, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(resumed.midi1_count == 0 && resumed.osc_count == 0);
}

// Resume after a pause with no edit: the sink sees exactly that tick's
// events, in wire order, and none of the preview's.
void test_tick_cap9_resume_without_edit(void) {
  Recording resumed, control;
  Usz preview_count, control_preview_count;
  run_cap9(false, true, &resumed, &preview_count);
  run_cap9(false, false, &control, &control_preview_count);
  CHECK(preview_count == 3); // the note, the CC and the pitch bend
  Triple const want[] = {{0xB0, 74, 64}, {0xE0, 127, 61}, {0x90, 36, 127}};
  CHECK(triples_are(&resumed, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(recordings_equal(&resumed, &control));
  CHECK(resumed.midi1_count == 0 && resumed.osc_count == 0);
}

// The wire order inside one tick body. Four sustained notes are seeded, with
// their counts in ticks: one due to age out (1), one that the grid's note
// retriggers (10), one on the channel of the grid's mono note (10), and one
// that this tick ages but does not release (2). The sink receives the aged
// note-off, the CC and the pitch bend in VM order, the retrigger note-off,
// the note-on, the mono channel's note-off, then the mono note-on. (Step 1,
// the shell's F8 in ged_do_stuff, is covered by the manual check;
// tick_note_length_beat_clock only models it in test code. Step 3 is covered
// by tick_glide_engine_order.)
void test_tick_wire_order(void) {
  enum { H = 3, W = 32 };
  Glyph grid[H * W];
  Mark marks[H * W];
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 0, "D1");
  put(grid, W, 2, 0, "!0.74g."); // CC 74 = 64 on channel 0
  put(grid, W, 0, 8, "D1");
  put(grid, W, 2, 8, "?0hz"); // pitch bend on channel 0
  put(grid, W, 0, 13, "D1");
  put(grid, W, 2, 13, ":13C.4"); // note 36 on channel 1
  put(grid, W, 0, 20, "D1");
  put(grid, W, 2, 20, "%23E.4"); // mono note 40 on channel 2

  Susnote_list susnotes;
  susnote_list_init(&susnotes);
  Susnote const seeded[] = {
      {.remaining = 1, .chan_note = 3 << 8 | 60},  // ages out this tick
      {.remaining = 10, .chan_note = 1 << 8 | 36}, // retriggered
      {.remaining = 10, .chan_note = 2 << 8 | 50}, // on the mono channel
      {.remaining = 2, .chan_note = 4 << 8 | 70},  // outlasts this tick
  };
  Usz start_removed, end_removed;
  CHECK(susnote_list_add_notes(&susnotes, seeded, ORCA_ARRAY_COUNTOF(seeded),
                               &start_removed, &end_removed));
  CHECK(susnotes.count == 4 && start_removed == end_removed);

  Opstate_store store;
  opstate_init(&store);
  Oevent_list tick_list, engine_list;
  oevent_list_init(&tick_list);
  oevent_list_init(&engine_list);
  Usz tick_num = 0;
  Tick_ctx const ctx = {.gbuffer = grid,
                        .mbuffer = marks,
                        .height = H,
                        .width = W,
                        .random_seed = 0,
                        .opstate = &store,
                        .tick_num = &tick_num,
                        .tick_list = &tick_list,
                        .engine_list = &engine_list,
                        .susnotes = &susnotes};
  Recording rec;
  Tick_sink const sink = rec_sink(&rec);
  tick_body(&ctx, &sink);
  CHECK(engine_list.count == 0);

  Triple const want[] = {
      {0x83, 60, 0},   // 2. the note-off that aged out
      {0xB0, 74, 64},  // 4. the VM's CC,
      {0xE0, 127, 61}, //    then its pitch bend, in VM order
      {0x81, 36, 0},   // 5. the retriggered note's note-off,
      {0x91, 36, 127}, //    then the note-on
      {0x82, 50, 0},   // 6. the mono channel's sustained note-off,
      {0x92, 40, 127}, //    then the mono note-on
  };
  CHECK(triples_are(&rec, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(rec.midi1_count == 0 && rec.osc_count == 0);
  CHECK(tick_num == 1);
  CHECK(tick_list.count == 4);
  // The retriggered and the mono note each hold their duration (4) in ticks,
  // set after this body's aging step, and the fourth note lost one tick.
  CHECK(susnotes.count == 3);
  Usz lengths_ok = 0, aged_ok = 0;
  for (Usz i = 0; i < susnotes.count; ++i) {
    U16 cn = susnotes.buffer[i].chan_note;
    U8 rem = susnotes.buffer[i].remaining;
    if ((cn == (1 << 8 | 36) || cn == (2 << 8 | 40)) && rem == 4)
      ++lengths_ok;
    if (cn == (4 << 8 | 70) && rem == 1)
      ++aged_ok;
  }
  CHECK(lengths_ok == 2);
  CHECK(aged_ok == 1);

  susnote_list_deinit(&susnotes);
  oevent_list_deinit(&tick_list);
  oevent_list_deinit(&engine_list);
  opstate_free(&store);
}

// Step 3, the glide engine: a CCI the VM emits on one tick registers a
// glide, and the next tick body sends the engine's CC after the note-offs
// that age out and before the VM's events. Rate z is a one-step glide: one
// engine CC, at the target. The test then drains sim.c's global glide table,
// so later tests start with it inactive.
void test_tick_glide_engine_order(void) {
  enum { H = 3, W = 24 };
  Glyph grid[H * W];
  Mark marks[H * W];
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 0, "D1");
  put(grid, W, 2, 0, "!0.74gz"); // a glide to CC 74 = 64 on channel 0
  put(grid, W, 0, 8, "D1");
  put(grid, W, 2, 8, "!0.71g."); // an instant CC 71 = 64
  put(grid, W, 0, 16, "D1");
  put(grid, W, 2, 16, ":03C.1"); // note 36, one tick long

  Opstate_store store;
  opstate_init(&store);
  Oevent_list tick_list, engine_list;
  oevent_list_init(&tick_list);
  oevent_list_init(&engine_list);
  Susnote_list susnotes;
  susnote_list_init(&susnotes);
  Usz tick_num = 0;
  Tick_ctx const ctx = {.gbuffer = grid,
                        .mbuffer = marks,
                        .height = H,
                        .width = W,
                        .random_seed = 0,
                        .opstate = &store,
                        .tick_num = &tick_num,
                        .tick_list = &tick_list,
                        .engine_list = &engine_list,
                        .susnotes = &susnotes};

  // The first tick registers the glide; the engine has nothing to run yet.
  Recording first;
  Tick_sink const first_sink = rec_sink(&first);
  tick_body(&ctx, &first_sink);
  CHECK(engine_list.count == 0);
  Triple const want_first[] = {{0xB0, 71, 64}, {0x90, 36, 127}};
  CHECK(triples_are(&first, want_first, ORCA_ARRAY_COUNTOF(want_first)));

  // The second tick runs it.
  Recording second;
  Tick_sink const second_sink = rec_sink(&second);
  tick_body(&ctx, &second_sink);
  CHECK(engine_list.count == 1);
  Triple const want_second[] = {
      {0x80, 36, 0},   // 2. the note-off that aged out
      {0xB0, 74, 64},  // 3. the engine's CC
      {0xB0, 71, 64},  // 4. the VM's CC
      {0x90, 36, 127}, // 5. the VM's note-on
  };
  CHECK(triples_are(&second, want_second, ORCA_ARRAY_COUNTOF(want_second)));
  CHECK(second.midi1_count == 0 && second.osc_count == 0);

  // The second tick's CCI registered the glide again: run the engine until
  // it emits nothing, so the global table is left inactive. The engine
  // advances one step per call and ignores its time argument, as in
  // tick_body.
  Oevent_list drain;
  oevent_list_init(&drain);
  Usz rounds = 0;
  do {
    oevent_list_clear(&drain);
    advance_midi_cc_interpolations(0.0, &drain);
    ++rounds;
  } while (drain.count > 0 && rounds < 64);
  CHECK(drain.count == 0);

  oevent_list_deinit(&drain);
  susnote_list_deinit(&susnotes);
  oevent_list_deinit(&tick_list);
  oevent_list_deinit(&engine_list);
  opstate_free(&store);
}

// Release-all (pause, quit, an output change) sends a note-off for every
// sustained note and clears the list, whatever their counts.
void test_tick_release_all(void) {
  Susnote_list susnotes;
  susnote_list_init(&susnotes);
  Susnote const seeded[] = {
      {.remaining = 3, .chan_note = 0 << 8 | 36},
      {.remaining = 20, .chan_note = 9 << 8 | 127},
  };
  Usz start_removed, end_removed;
  CHECK(susnote_list_add_notes(&susnotes, seeded, ORCA_ARRAY_COUNTOF(seeded),
                               &start_removed, &end_removed));
  Recording rec;
  Tick_sink const sink = rec_sink(&rec);
  tick_release_all(&sink, &susnotes);
  sort_triples(&rec);
  Triple const want[] = {{0x80, 36, 0}, {0x89, 127, 0}};
  CHECK(triples_are(&rec, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(susnotes.count == 0);
  // With nothing sustained it sends nothing.
  Tick_sink const again = rec_sink(&rec);
  tick_release_all(&again, &susnotes);
  CHECK(rec.midi3_count == 0);
  susnote_list_deinit(&susnotes);
}

// The note-length fixtures (B8). The grid holds D operators over note
// operators. Dz bangs on ticks that are multiples of 35 only, so each of its
// notes sounds once in a 35-tick window and no retrigger falls inside it.
enum { Note_h = 3, Note_w = 32, Note_window = 35 };

typedef struct {
  Usz body; // the tick body, counted from the first one run
  Triple t;
} Stamped;

static void note_grid(Glyph *grid) {
  memset(grid, '.', (Usz)Note_h * Note_w);
}

// Runs `bodies` tick bodies over grid from tick `start`, recording into rec,
// with every MIDI triple stamped with the body it went out in. With
// beat_clock set, it models the shell's sixths gate (ged_do_stuff in
// tui_main.c), which stays in the shell (AD-14): each tick is six pulses,
// each pulse sends F8 through midi1, and the tick body runs after the F8 of
// the pulse whose sixth is 0, so five more F8s follow it.
static void run_note_bodies(Glyph *grid, Usz start, Usz bodies,
                            bool beat_clock, Recording *rec) {
  Mark marks[Note_h * Note_w];
  Opstate_store store;
  opstate_init(&store);
  Oevent_list tick_list, engine_list;
  oevent_list_init(&tick_list);
  oevent_list_init(&engine_list);
  Susnote_list susnotes;
  susnote_list_init(&susnotes);
  Usz tick_num = start;
  Tick_ctx const ctx = {.gbuffer = grid,
                        .mbuffer = marks,
                        .height = Note_h,
                        .width = Note_w,
                        .random_seed = 0,
                        .opstate = &store,
                        .tick_num = &tick_num,
                        .tick_list = &tick_list,
                        .engine_list = &engine_list,
                        .susnotes = &susnotes};
  Tick_sink const sink = rec_sink(rec);
  U8 bclock_sixths = 0; // as Ged.midi_bclock_sixths, 0 when play starts
  for (Usz body = 0; body < bodies; ++body) {
    rec->body = body;
    if (!beat_clock) {
      tick_body(&ctx, &sink);
    } else {
      for (int pulse = 0; pulse < 6; ++pulse) {
        sink.midi1(sink.u, 0xF8);
        U8 sixths = bclock_sixths;
        bclock_sixths = (U8)((sixths + 1) % 6);
        if (sixths == 0)
          tick_body(&ctx, &sink);
      }
    }
    CHECK(engine_list.count == 0);
  }
  CHECK(tick_num == start + bodies);
  susnote_list_deinit(&susnotes);
  oevent_list_deinit(&tick_list);
  oevent_list_deinit(&engine_list);
  opstate_free(&store);
}

// The recording holds exactly these triples, in this order, each in its
// tick body.
static bool timeline_is(Recording const *r, Stamped const *want, Usz count) {
  if (r->overflow || r->midi3_count != count)
    return false;
  for (Usz i = 0; i < count; ++i) {
    if (r->midi3_body[i] != want[i].body ||
        triple_cmp(r->midi3[i], want[i].t) != 0)
      return false;
  }
  return true;
}

// B8, beat clock off: a duration-4 note that sounds in tick body T goes off
// in body T + 4, and nothing else goes out in the window.
void test_tick_note_length(void) {
  Glyph grid[Note_h * Note_w];
  note_grid(grid);
  put(grid, Note_w, 0, 0, "Dz");
  put(grid, Note_w, 2, 0, ":03C.4"); // note 36, 4 ticks
  Recording rec;
  run_note_bodies(grid, 0, Note_window, false, &rec);
  Stamped const want[] = {{0, {0x90, 36, 127}}, {4, {0x80, 36, 0}}};
  CHECK(timeline_is(&rec, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(rec.midi1_count == 0 && rec.osc_count == 0);
}

// B8, beat clock on: the same note goes off in the same tick body, 24 F8s
// after its note-on, where 1295c23 aged it by a sixth of a tick per body and
// sent its note-off 144 F8s after.
void test_tick_note_length_beat_clock(void) {
  Glyph grid[Note_h * Note_w];
  note_grid(grid);
  put(grid, Note_w, 0, 0, "Dz");
  put(grid, Note_w, 2, 0, ":03C.4");
  Recording rec;
  run_note_bodies(grid, 0, Note_window, true, &rec);
  Stamped const want[] = {{0, {0x90, 36, 127}}, {4, {0x80, 36, 0}}};
  CHECK(timeline_is(&rec, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(rec.midi1_count == 6 * Note_window);
  // The F8s are this test's model of the shell's gate, not tick.c output.
  CHECK(rec.midi3_count >= 1 && rec.midi3_pulses[0] == 1);
  CHECK(rec.midi3_count >= 2 &&
        rec.midi3_pulses[1] - rec.midi3_pulses[0] == 24);
  CHECK(rec.osc_count == 0);
}

// B8: durations 0 and 1 both release in the next tick body, T + 1.
void test_tick_note_length_0_and_1(void) {
  Glyph grid[Note_h * Note_w];
  note_grid(grid);
  put(grid, Note_w, 0, 0, "Dz");
  put(grid, Note_w, 2, 0, ":03C.0"); // note 36, duration 0
  put(grid, Note_w, 0, 8, "Dz");
  put(grid, Note_w, 2, 8, ":03D.1"); // note 38, duration 1
  Recording rec;
  run_note_bodies(grid, 0, Note_window, false, &rec);
  Stamped const want[] = {
      {0, {0x90, 36, 127}}, {0, {0x90, 38, 127}}, // step 5, in VM order
      {1, {0x80, 38, 0}},   {1, {0x80, 36, 0}},   // step 2, swap order
  };
  CHECK(timeline_is(&rec, want, ORCA_ARRAY_COUNTOF(want)));
}

// B8: three notes that sound in one tick body and release in the same later
// body go off in the aging step's swap order: the 2nd, the 3rd, then the
// 1st, as in 1295c23.
void test_tick_note_release_order(void) {
  Glyph grid[Note_h * Note_w];
  note_grid(grid);
  put(grid, Note_w, 0, 0, "Dz");
  put(grid, Note_w, 2, 0, ":03C.4"); // 1st: note 36
  put(grid, Note_w, 0, 8, "Dz");
  put(grid, Note_w, 2, 8, ":03D.4"); // 2nd: note 38
  put(grid, Note_w, 0, 16, "Dz");
  put(grid, Note_w, 2, 16, ":03E.4"); // 3rd: note 40
  Recording rec;
  run_note_bodies(grid, 0, Note_window, false, &rec);
  Stamped const want[] = {
      {0, {0x90, 36, 127}}, {0, {0x90, 38, 127}}, {0, {0x90, 40, 127}},
      {4, {0x80, 38, 0}},   {4, {0x80, 40, 0}},   {4, {0x80, 36, 0}},
  };
  CHECK(timeline_is(&rec, want, ORCA_ARRAY_COUNTOF(want)));
}

// The retrigger and mono fixtures start at tick 35, where Dz bangs and 6D6
// (period 36) does not; 6D6 bangs at tick 36, one body later. Neither bangs
// again before tick 70, so the window holds one bang of each.
enum { Late_start = 35 };

// B8: the same channel and note sounds again before its release. The old
// note's off goes out at once (step 5), as in 1295c23; its old count is
// dropped, and the only later off is at the new end. Note 36 is retriggered
// longer (4 ticks, then 6) and note 38 shorter (8, then 3), so an old count
// that was kept, or that outlived the new one, would send an off at an old
// end (body 0 + 4 or 0 + 8) or miss a new one (body 1 + 6 or 1 + 3).
void test_tick_note_retrigger(void) {
  Glyph grid[Note_h * Note_w];
  note_grid(grid);
  put(grid, Note_w, 0, 0, "Dz");
  put(grid, Note_w, 2, 0, ":03C.4"); // note 36, 4 ticks, at tick 35
  put(grid, Note_w, 0, 8, "Dz");
  put(grid, Note_w, 2, 8, ":03D.8"); // note 38, 8 ticks, at tick 35
  put(grid, Note_w, 0, 15, "6D6");
  put(grid, Note_w, 2, 16, ":03C.6"); // note 36 again, 6 ticks, at tick 36
  put(grid, Note_w, 0, 23, "6D6");
  put(grid, Note_w, 2, 24, ":03D.3"); // note 38 again, 3 ticks, at tick 36
  Recording rec;
  run_note_bodies(grid, Late_start, Note_window, false, &rec);
  Stamped const want[] = {
      {0, {0x90, 36, 127}}, // the first notes, in VM order
      {0, {0x90, 38, 127}},
      {1, {0x80, 36, 0}},   // step 5: the old notes' offs, at once,
      {1, {0x80, 38, 0}},
      {1, {0x90, 36, 127}}, //         then the new note-ons
      {1, {0x90, 38, 127}},
      {4, {0x80, 38, 0}},   // note 38's new end, body 1 + 3
      {7, {0x80, 36, 0}},   // note 36's new end, body 1 + 6
  };
  CHECK(timeline_is(&rec, want, ORCA_ARRAY_COUNTOF(want)));
}

// B8: a mono note on a channel with a sustained note. The mono round sends
// the sustained note's off, then the mono note-on (step 6), as in 1295c23;
// the mono note's count is its duration, so it goes off 3 bodies later, and
// the removed note sends nothing at its old end (body 0 + 8).
void test_tick_note_mono(void) {
  Glyph grid[Note_h * Note_w];
  note_grid(grid);
  put(grid, Note_w, 0, 0, "Dz");
  put(grid, Note_w, 2, 0, ":03E.8"); // note 40, 8 ticks, at tick 35
  put(grid, Note_w, 0, 8, "6D6");
  put(grid, Note_w, 2, 9, "%03C.3"); // mono note 36, 3 ticks, at tick 36
  Recording rec;
  run_note_bodies(grid, Late_start, Note_window, false, &rec);
  Stamped const want[] = {
      {0, {0x90, 40, 127}},
      {1, {0x80, 40, 0}},   // step 6: the channel's sustained note-off,
      {1, {0x90, 36, 127}}, //         then the mono note-on
      {4, {0x80, 36, 0}},   // the mono note's end
  };
  CHECK(timeline_is(&rec, want, ORCA_ARRAY_COUNTOF(want)));
}

// B8: with no sustained notes, release-all and the aging step send nothing
// and touch no buffer: a list that never held a note keeps its NULL buffer.
// The empty-list guards themselves (no NULL + 0) are checked by reading the
// code: GCC's UBSan does not report NULL + 0, so this test cannot see them.
void test_tick_note_empty_list(void) {
  Susnote_list susnotes;
  susnote_list_init(&susnotes);
  Recording rec;
  Tick_sink const sink = rec_sink(&rec);
  tick_release_all(&sink, &susnotes);
  CHECK(rec.midi3_count == 0);
  CHECK(susnotes.buffer == NULL && susnotes.count == 0);

  // A tick body on a grid with no note operator ages the empty list.
  enum { H = Note_h, W = Note_w };
  Glyph grid[H * W];
  Mark marks[H * W];
  note_grid(grid);
  put(grid, W, 0, 0, "D1");
  put(grid, W, 2, 0, "!0.74g."); // an instant CC, so the body sends something
  Opstate_store store;
  opstate_init(&store);
  Oevent_list tick_list, engine_list;
  oevent_list_init(&tick_list);
  oevent_list_init(&engine_list);
  Usz tick_num = 0;
  Tick_ctx const ctx = {.gbuffer = grid,
                        .mbuffer = marks,
                        .height = H,
                        .width = W,
                        .random_seed = 0,
                        .opstate = &store,
                        .tick_num = &tick_num,
                        .tick_list = &tick_list,
                        .engine_list = &engine_list,
                        .susnotes = &susnotes};
  tick_body(&ctx, &sink);
  tick_body(&ctx, &sink);
  CHECK(engine_list.count == 0);
  Triple const want[] = {{0xB0, 74, 64}, {0xB0, 74, 64}};
  CHECK(triples_are(&rec, want, ORCA_ARRAY_COUNTOF(want)));
  CHECK(susnotes.buffer == NULL && susnotes.count == 0);

  oevent_list_deinit(&tick_list);
  oevent_list_deinit(&engine_list);
  opstate_free(&store);
  susnote_list_deinit(&susnotes);
}

// tick_run_vm, which step-forward (Ctrl+F) and the preview use, clears the
// list, then fills it with one VM run's events.
void test_tick_run_vm_clears_list(void) {
  enum { H = Cap9_h, W = Cap9_w };
  Glyph grid[H * W];
  Mark marks[H * W];
  cap9_patch(grid);
  Opstate_store store;
  opstate_init(&store);
  Oevent_list tick_list;
  oevent_list_init(&tick_list);
  Oevent *stale = oevent_list_alloc_item(&tick_list);
  stale->midi_cc = (Oevent_midi_cc){.oevent_type = Oevent_type_midi_cc,
                                    .channel = 5,
                                    .control = 1,
                                    .value = 2};

  tick_run_vm(grid, marks, H, W, 0, &tick_list, 0, &store);
  CHECK(tick_list.count == 3); // the note, the CC and the pitch bend
  bool saw_stale = false;
  for (Usz i = 0; i < tick_list.count; ++i) {
    Oevent const *e = &tick_list.buffer[i];
    if (e->any.oevent_type == Oevent_type_midi_cc && e->midi_cc.channel == 5)
      saw_stale = true;
  }
  CHECK(!saw_stale);

  oevent_list_deinit(&tick_list);
  opstate_free(&store);
}

// tick_len_us: one sixteenth in microseconds, rounded half-up, never below 1.
void test_tick_len_us(void) {
  CHECK(tick_len_us(120) == 125000);
  CHECK(tick_len_us(1) == 15000000);
  CHECK(tick_len_us(7) == 2142857);       // 2142857.14, rounded down
  CHECK(tick_len_us(9) == 1666667);       // 1666666.67, rounded up
  CHECK(tick_len_us(10000000) == 2);      // 1.5: half rounds up
  CHECK(tick_len_us(30000000) == 1);      // 0.5: half rounds up
  CHECK(tick_len_us(30000001) == 1);      // below 0.5: clamped to 1
  CHECK(tick_len_us((Usz)SIZE_MAX) == 1); // a huge BPM: clamped to 1
  CHECK(tick_len_us(0) == 15000000);      // 0 counts as 1
}
