// Unit tests for the operator dialects (spec item I1, CAP-13; spine AD-4,
// AD-18): which list orca_run runs, the upstream-only bodies of !, r, ; and
// =, the inert $ and & upstream, and the dialect names that cli, orca and
// orca.conf share. tests/upstream/compare.sh checks the upstream dialect
// against Orca-c 9df9786 itself, byte for byte, on all 43 of its examples.
#include "../../ccout.h"
#include "../../gbuffer.h"
#include "../../opstate.h"
#include "../../sim.h"
#include "../../tick.h"
#include "../../vmio.h"
#include "tests.h"

// One tick as cli runs it: clear the marks and the event list, then run the
// VM with the store and the dialect.
static void run_tick(Glyph *grid, Mark *marks, Usz height, Usz width,
                     Usz tick, Oevent_list *events, Usz seed,
                     Opstate_store *store, Orca_dialect dialect) {
  mbuffer_clear(marks, height, width);
  oevent_list_clear(events);
  Orca_run_ctx const ctx = {.opstate = store, .dialect = dialect};
  orca_run(grid, marks, height, width, tick, events, seed, &ctx);
}

// Writes text into the grid at (y, x).
static void put(Glyph *grid, Usz width, Usz y, Usz x, char const *text) {
  memcpy(grid + y * width + x, text, strlen(text));
}

static Usz count_type(Oevent_list const *events, Oevent_types type) {
  Usz n = 0;
  for (Usz i = 0; i < events->count; ++i)
    n += events->buffer[i].any.oevent_type == type;
  return n;
}

// Only "borca" and "upstream" are dialect names, exactly; anything else
// leaves the output alone, which the flags and the conf key rely on.
void test_dialect_names(void) {
  Orca_dialect d = Orca_dialect_upstream;
  CHECK(orca_dialect_from_name("borca", &d) && d == Orca_dialect_borca);
  CHECK(orca_dialect_from_name("upstream", &d) && d == Orca_dialect_upstream);
  static char const *const bad[] = {"",        "bOrca",    "Borca",
                                    "Upstream", "upstream ", " borca",
                                    "orca-c",  "1"};
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(bad); ++i) {
    d = Orca_dialect_upstream;
    CHECK(!orca_dialect_from_name(bad[i], &d));
    CHECK(d == Orca_dialect_upstream);
  }
  CHECK(strcmp(orca_dialect_name(Orca_dialect_borca), "borca") == 0);
  CHECK(strcmp(orca_dialect_name(Orca_dialect_upstream), "upstream") == 0);
  // orca_run runs any other value as bOrca, and the name says so.
  CHECK(strcmp(orca_dialect_name((Orca_dialect)7), "borca") == 0);
  CHECK(Orca_dialect_borca == 0); // a zeroed context is bOrca (AD-4)
}

// A context that names no dialect runs bOrca's list. Three copies of a grid
// with every glyph whose body differs between the dialects run 16 ticks: [0]
// with a context that sets only the store, [1] with Orca_dialect_borca and
// [2] with Orca_dialect_upstream. On every tick, [0] and [1] have the same
// grid, the same marks, the same events (byte for byte) and the same number
// of store entries. Summed over the ticks, [1] sends bOrca's CCs and notes
// and keeps three entries (;, & and r), and [2] sends UDP, no CC and keeps
// none.
void test_dialect_default_is_borca(void) {
  enum { H = 10, W = 12, Ticks = 16 };
  // !, $, ;, =, & and r, each banged from below where it needs a bang.
  static char const *const rows[H] = {
      "!1..7g......", //
      "*...........", //
      "$3c02.......", //
      "............", //
      ";12.........", //
      "*...........", //
      "=13Caf1.....", //
      "*...........", //
      "&0z12...0r9.", //
      "............", //
  };
  // The bangs, put back before each tick: an output or the bang's own
  // tick replaces each one.
  static Usz const bangs[][2] = {{1, 0}, {5, 0}, {7, 0}, {9, 9}};
  Glyph grids[3][H * W];
  Mark marks[3][H * W];
  Oevent_list events[3];
  Opstate_store stores[3];
  Usz ccs[3] = {0}, notes[3] = {0}, udps[3] = {0};
  for (Usz k = 0; k < 3; ++k) {
    for (Usz y = 0; y < H; ++y)
      put(grids[k], W, y, 0, rows[y]);
    oevent_list_init(&events[k]);
    opstate_init(&stores[k]);
  }
  for (Usz tick = 0; tick < Ticks; ++tick) {
    for (Usz k = 0; k < 3; ++k)
      for (Usz b = 0; b < ORCA_ARRAY_COUNTOF(bangs); ++b)
        grids[k][bangs[b][0] * W + bangs[b][1]] = '*';
    // [0]: a context with only the store, as every caller built it before
    // the dialect field existed.
    mbuffer_clear(marks[0], H, W);
    oevent_list_clear(&events[0]);
    Orca_run_ctx const zeroed = {.opstate = &stores[0]};
    orca_run(grids[0], marks[0], H, W, tick, &events[0], 3, &zeroed);
    run_tick(grids[1], marks[1], H, W, tick, &events[1], 3, &stores[1],
             Orca_dialect_borca);
    run_tick(grids[2], marks[2], H, W, tick, &events[2], 3, &stores[2],
             Orca_dialect_upstream);
    CHECK(memcmp(grids[0], grids[1], sizeof grids[0]) == 0);
    CHECK(memcmp(marks[0], marks[1], sizeof marks[0]) == 0);
    CHECK(events[0].count == events[1].count);
    CHECK(events[0].count == 0 ||
          memcmp(events[0].buffer, events[1].buffer,
                 events[0].count * sizeof(Oevent)) == 0);
    CHECK(stores[0].count == stores[1].count);
    for (Usz k = 0; k < 3; ++k) {
      ccs[k] += count_type(&events[k], Oevent_type_midi_cc);
      notes[k] += count_type(&events[k], Oevent_type_midi_note);
      udps[k] += count_type(&events[k], Oevent_type_udp_string);
    }
  }
  // bOrca's ! sends a CC and = notes on every tick, and its ;, & and r keep
  // per-cell state; neither sends UDP.
  CHECK(ccs[1] == Ticks && notes[1] > 0 && udps[1] == 0);
  CHECK(stores[1].count == 3);
  // Upstream's ; sends UDP on every tick, its ! sends nothing without a
  // control, and nothing keeps state.
  CHECK(udps[2] == Ticks && ccs[2] == 0);
  CHECK(stores[2].count == 0);
  for (Usz k = 0; k < 3; ++k) {
    oevent_list_deinit(&events[k]);
    opstate_free(&stores[k]);
  }
}

// Upstream ! (Orca-c 9df9786's midicc): three inputs, channel, control and
// value, and a plain CC whose value is the glyph's 0-35 times 127 / 35. It
// never glides, and a fourth glyph east of it is not one of its ports.
void test_dialect_upstream_cc(void) {
  enum { H = 2, W = 8 };
  static struct {
    char const *row; // banged from below
    bool sends;
    U8 channel, control, value;
  } const cases[] = {
      {"!1az", true, 1, 10, 127},  // z is 35: 35 * 127 / 35
      {"!12h", true, 1, 2, 61},    // h is 17: 2159 / 35 = 61.7
      {"!f21", true, 15, 2, 3},    // 1: 127 / 35 = 3.6
      {"!120", true, 1, 2, 0},     //
      {"!12.", true, 1, 2, 0},     // an empty value is 0, and still sends
      {"!12z5", true, 1, 2, 127},  // the 5 is no glide rate: a plain CC
      {"!12Z", true, 1, 2, 127},   // uppercase reads like lowercase
      {"!g2z", false, 0, 0, 0},    // channel 16: nothing
      {"!.2z", false, 0, 0, 0},    // no channel: nothing
      {"!1.z", false, 0, 0, 0},    // no control: nothing
  };
  Glyph grid[H * W];
  Mark marks[H * W];
  Oevent_list events;
  oevent_list_init(&events);
  Opstate_store store;
  opstate_init(&store);
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(cases); ++i) {
    memset(grid, '.', sizeof grid);
    put(grid, W, 0, 1, cases[i].row);
    put(grid, W, 1, 1, "*");
    run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
    if (!cases[i].sends) {
      CHECK(events.count == 0);
      continue;
    }
    CHECK(events.count == 1);
    if (events.count != 1)
      continue;
    Oevent_midi_cc const *cc = &events.buffer[0].midi_cc;
    CHECK(cc->oevent_type == Oevent_type_midi_cc);
    CHECK(cc->channel == cases[i].channel);
    CHECK(cc->control == cases[i].control);
    CHECK(cc->value == cases[i].value);
    // Ports at +1 to +3, locked; none at +4, where bOrca's ! reads a digit.
    for (Usz dx = 2; dx <= 4; ++dx)
      CHECK((marks[dx] & Mark_flag_lock) && (marks[dx] & Mark_flag_input));
    CHECK(!(marks[5] & (Mark_flag_lock | Mark_flag_input)));
  }
  // Not banged: nothing, though its ports are still marked.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, "!1az");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 0);
  CHECK(marks[2] & Mark_flag_input);
  // Every value glyph, 0 to z.
  for (Usz v = 0; v < 36; ++v) {
    memset(grid, '.', sizeof grid);
    put(grid, W, 0, 1, "!12");
    grid[4] = (Glyph)(v < 10 ? '0' + (int)v : 'a' + (int)v - 10);
    put(grid, W, 1, 1, "*");
    run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
    CHECK(events.count == 1 &&
          events.buffer[0].midi_cc.oevent_type == Oevent_type_midi_cc &&
          events.buffer[0].midi_cc.value == (U8)(v * 127 / 35));
  }
  CHECK(store.count == 0);
  oevent_list_deinit(&events);
  opstate_free(&store);
}

// Upstream r is a banged R: on every tick it outputs what R outputs at the
// same cell, seed and tick, with R's exclusive max, and keeps no state.
// Unbanged, it does nothing.
void test_dialect_upstream_r(void) {
  enum { H = 3, W = 5, Ticks = 200 };
  static char const *const inputs[] = {"0r9", "3rc", "arC", "9r2", "5r5",
                                       "0r0"};
  Glyph r_grid[H * W], R_grid[H * W];
  Mark marks[H * W];
  Oevent_list events;
  oevent_list_init(&events);
  Opstate_store store;
  opstate_init(&store);
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(inputs); ++i) {
    for (Usz seed = 0; seed < 8; seed += 7) {
      memset(r_grid, '.', sizeof r_grid);
      put(r_grid, W, 1, 1, inputs[i]);
      memcpy(R_grid, r_grid, sizeof R_grid);
      R_grid[1 * W + 2] = 'R';
      bool saw_max = false, saw_nonmin = false;
      for (Usz tick = 0; tick < Ticks; ++tick) {
        // The bang sits in the output cell, below, which the output then
        // overwrites; R, which needs no bang, gets the same cell.
        r_grid[2 * W + 2] = '*';
        R_grid[2 * W + 2] = '*';
        run_tick(r_grid, marks, H, W, tick, &events, seed, &store,
                 Orca_dialect_upstream);
        run_tick(R_grid, marks, H, W, tick, &events, seed, &store,
                 Orca_dialect_upstream);
        Glyph out = r_grid[2 * W + 2];
        CHECK(out == R_grid[2 * W + 2]);
        CHECK(out != '*');
        if (out == inputs[i][2] && inputs[i][0] != inputs[i][2])
          saw_max = true;
        if (out != inputs[i][0])
          saw_nonmin = true;
      }
      // "0r9" and "3rc" output from min up to max, never max itself.
      if (i < 2)
        CHECK(!saw_max && saw_nonmin);
    }
  }
  CHECK(store.count == 0);
  // The uppercase max's case: "brC" outputs B, the only value from b up to
  // C, but not C, in uppercase.
  memset(r_grid, '.', sizeof r_grid);
  put(r_grid, W, 1, 1, "brC");
  r_grid[2 * W + 2] = '*';
  run_tick(r_grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(r_grid[2 * W + 2] == 'B');
  // Not banged: the output cell keeps its glyph.
  memset(r_grid, '.', sizeof r_grid);
  put(r_grid, W, 1, 1, "0r9");
  run_tick(r_grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(r_grid[2 * W + 2] == '.');
  // bOrca's r, banged the same way, keeps its bag in the store.
  r_grid[2 * W + 2] = '*';
  run_tick(r_grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_borca);
  CHECK(store.count == 1);
  oevent_list_deinit(&events);
  opstate_free(&store);
}

// Upstream ; sends the glyphs east of it, up to a '.' and at most 16, as one
// UDP string, and locks them banged or not. Upstream = sends an OSC message
// to /<path glyph> with count values.
void test_dialect_upstream_udp_osc(void) {
  enum { H = 2, W = 24 };
  Glyph grid[H * W];
  Mark marks[H * W];
  Oevent_list events;
  oevent_list_init(&events);
  Opstate_store store;
  opstate_init(&store);

  // 20 digits east of ;: 16 sent and locked, the 17th neither.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, ";01234567890123456789");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 1);
  if (events.count == 1) {
    Oevent_udp_string const *u = &events.buffer[0].udp_string;
    CHECK(u->oevent_type == Oevent_type_udp_string);
    CHECK(u->count == 16);
    CHECK(memcmp(u->chars, "0123456789012345", 16) == 0);
  }
  for (Usz x = 2; x < 18; ++x)
    CHECK(marks[x] & Mark_flag_lock);
  CHECK(!(marks[18] & Mark_flag_lock));

  // It stops at the first '.', and locks only what it read.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, ";HI.99");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 1 && events.buffer[0].udp_string.count == 2 &&
        memcmp(events.buffer[0].udp_string.chars, "HI", 2) == 0);
  CHECK((marks[2] & Mark_flag_lock) && (marks[3] & Mark_flag_lock));
  CHECK(!(marks[5] & Mark_flag_lock));

  // Not banged: no event, but the glyphs are locked all the same.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, ";HI");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 0);
  CHECK((marks[2] & Mark_flag_lock) && (marks[3] & Mark_flag_lock));

  // = with path a and three values.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, "=a3123");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 1);
  if (events.count == 1) {
    Oevent_osc_ints const *o = &events.buffer[0].osc_ints;
    CHECK(o->oevent_type == Oevent_type_osc_ints);
    CHECK(o->glyph == 'a');
    CHECK(o->count == 3);
    CHECK(o->numbers[0] == 1 && o->numbers[1] == 2 && o->numbers[2] == 3);
  }
  // Its value ports are locked, and nothing past them.
  CHECK((marks[4] & Mark_flag_lock) && (marks[6] & Mark_flag_lock));
  CHECK(!(marks[7] & Mark_flag_lock));
  // No path: nothing. A count of 0: a message with no values.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, "=.3123");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 0);
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, "=b0");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_upstream);
  CHECK(events.count == 1 && events.buffer[0].osc_ints.glyph == 'b' &&
        events.buffer[0].osc_ints.count == 0);

  // bOrca's ; and = on the same grids send neither UDP nor OSC.
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, ";12");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_borca);
  CHECK(count_type(&events, Oevent_type_udp_string) == 0);
  memset(grid, '.', sizeof grid);
  put(grid, W, 0, 1, "=a3123");
  put(grid, W, 1, 1, "*");
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_borca);
  CHECK(count_type(&events, Oevent_type_osc_ints) == 0);

  // At the east edge. Rows 1 to 5 hold what reading or marking past the
  // end of row 0 would wrap onto: digits, and the bang, which marks nothing.
  enum { EH = 6, EW = 8 };
  Glyph edge[EH * EW];
  Mark edge_marks[EH * EW];
  // ; three cells from the edge: two glyphs, both locked, nothing past them.
  memset(edge, '.', sizeof edge);
  put(edge, EW, 0, 5, ";AB");
  put(edge, EW, 1, 0, "12");
  put(edge, EW, 1, 5, "*");
  run_tick(edge, edge_marks, EH, EW, 0, &events, 0, &store,
           Orca_dialect_upstream);
  CHECK(events.count == 1 && events.buffer[0].udp_string.count == 2 &&
        memcmp(events.buffer[0].udp_string.chars, "AB", 2) == 0);
  CHECK((edge_marks[6] & Mark_flag_lock) && (edge_marks[7] & Mark_flag_lock));
  for (Usz i = EW; i < EH * EW; ++i)
    CHECK(edge_marks[i] == 0);
  // = three cells from the edge with count z (35): every value port is past
  // the edge, so none is marked, and each value reads as 0.
  memset(edge, '.', sizeof edge);
  put(edge, EW, 0, 5, "=az");
  put(edge, EW, 1, 0, "12");
  put(edge, EW, 1, 5, "*");
  run_tick(edge, edge_marks, EH, EW, 0, &events, 0, &store,
           Orca_dialect_upstream);
  CHECK(events.count == 1);
  if (events.count == 1) {
    Oevent_osc_ints const *o = &events.buffer[0].osc_ints;
    CHECK(o->glyph == 'a' && o->count == Oevent_osc_int_count);
    bool zeros = true;
    for (Usz i = 0; i < o->count; ++i)
      zeros &= o->numbers[i] == 0;
    CHECK(zeros);
  }
  CHECK((edge_marks[6] & Mark_flag_lock) && (edge_marks[7] & Mark_flag_lock));
  for (Usz i = EW; i < EH * EW; ++i)
    CHECK(edge_marks[i] == 0);
  oevent_list_deinit(&events);
  opstate_free(&store);
}

// Upstream has no $ or &: they write nothing, mark nothing and keep no
// state, where bOrca's write their outputs.
void test_dialect_upstream_inert(void) {
  enum { H = 4, W = 8 };
  // Digits and unbanged lowercase operators only, so nothing else runs.
  static char const *const rows[H] = {".$3c02..", "........", ".&0z12..",
                                      "........"};
  Glyph grid[H * W], before[H * W];
  Mark marks[H * W];
  Oevent_list events;
  oevent_list_init(&events);
  Opstate_store store;
  opstate_init(&store);
  for (Usz y = 0; y < H; ++y)
    put(before, W, y, 0, rows[y]);
  memcpy(grid, before, sizeof grid);
  for (Usz tick = 0; tick < 8; ++tick) {
    run_tick(grid, marks, H, W, tick, &events, 0, &store,
             Orca_dialect_upstream);
    CHECK(memcmp(grid, before, sizeof grid) == 0);
    CHECK(events.count == 0);
    bool marked = false;
    for (Usz i = 0; i < H * W; ++i)
      marked |= marks[i] != 0;
    CHECK(!marked);
  }
  CHECK(store.count == 0);
  // bOrca: $3c02 writes its octave and note below, and & writes its output
  // and keeps state.
  run_tick(grid, marks, H, W, 0, &events, 0, &store, Orca_dialect_borca);
  CHECK(memcmp(grid, before, sizeof grid) != 0);
  CHECK(store.count == 1);
  oevent_list_deinit(&events);
  opstate_free(&store);
}

// Records what tick_body sends: each MIDI triple, and the type of each event
// that reaches the sink's osc callback.
typedef struct {
  int midi3[8][3];
  Usz midi3_count;
  Usz midi1_count;
  U8 osc_types[8];
  Usz osc_count;
} Dialect_recording;

static void dialect_rec_midi3(void *u, int status, int d1, int d2) {
  Dialect_recording *r = u;
  if (r->midi3_count < ORCA_ARRAY_COUNTOF(r->midi3)) {
    r->midi3[r->midi3_count][0] = status;
    r->midi3[r->midi3_count][1] = d1;
    r->midi3[r->midi3_count][2] = d2;
  }
  ++r->midi3_count;
}

static void dialect_rec_midi1(void *u, int byte) {
  Dialect_recording *r = u;
  (void)byte;
  ++r->midi1_count;
}

static void dialect_rec_osc(void *u, Oevent const *e) {
  Dialect_recording *r = u;
  if (r->osc_count < ORCA_ARRAY_COUNTOF(r->osc_types))
    r->osc_types[r->osc_count] = e->any.oevent_type;
  ++r->osc_count;
}

static Usz recorded_osc_type(Dialect_recording const *r, Oevent_types type) {
  Usz n = 0;
  for (Usz i = 0; i < r->osc_count && i < ORCA_ARRAY_COUNTOF(r->osc_types);
       ++i)
    n += r->osc_types[i] == type;
  return n;
}

// The tick body carries Tick_ctx's dialect to the VM: in the upstream
// dialect a banged ;HI and a banged = with values reach the sink's osc
// callback as one UDP and one OSC event, and a banged ! goes out as one
// plain CC (0xB0), at once, with no glide left in the engine. The same grid
// in bOrca's dialect sends no OSC or UDP.
void test_dialect_tick_body_upstream(void) {
  enum { H = 6, W = 8 };
  static char const *const rows[H] = {
      ";HI.....", // UDP "HI"
      "*.......", //
      "=a212...", // OSC /a 1 2
      "*.......", //
      "!02h....", // CC channel 0, control 2, h (17) x 127 / 35 = 61
      "*.......", //
  };
  for (int pass = 0; pass < 2; ++pass) {
    Orca_dialect dialect =
        pass == 0 ? Orca_dialect_upstream : Orca_dialect_borca;
    Glyph grid[H * W];
    Mark marks[H * W];
    for (Usz y = 0; y < H; ++y)
      put(grid, W, y, 0, rows[y]);
    Opstate_store store;
    opstate_init(&store);
    Oevent_list tick_list, engine_list;
    oevent_list_init(&tick_list);
    oevent_list_init(&engine_list);
    Susnote_list susnotes;
    susnote_list_init(&susnotes);
    Ccout_engine ccout;
    ccout_init(&ccout);
    Usz tick_num = 0;
    Tick_ctx const ctx = {.gbuffer = grid,
                          .mbuffer = marks,
                          .height = H,
                          .width = W,
                          .random_seed = 0,
                          .opstate = &store,
                          .dialect = dialect,
                          .tick_num = &tick_num,
                          .tick_list = &tick_list,
                          .engine_list = &engine_list,
                          .susnotes = &susnotes,
                          .ccout = &ccout,
                          .now_us = 0,
                          .tick_len = 125000};
    Dialect_recording rec;
    memset(&rec, 0, sizeof rec);
    Tick_sink const sink = {.u = &rec,
                            .midi3 = dialect_rec_midi3,
                            .midi1 = dialect_rec_midi1,
                            .osc = dialect_rec_osc};
    tick_body(&ctx, &sink);
    CHECK(tick_num == 1);
    if (pass == 0) {
      CHECK(rec.osc_count == 2);
      CHECK(recorded_osc_type(&rec, Oevent_type_udp_string) == 1);
      CHECK(recorded_osc_type(&rec, Oevent_type_osc_ints) == 1);
      CHECK(rec.midi3_count == 1);
      CHECK(rec.midi3[0][0] == 0xB0 && rec.midi3[0][1] == 2 &&
            rec.midi3[0][2] == 61);
      U64 t;
      CHECK(!ccout_next_deadline(&ccout, 0, &t) && ccout.active_count == 0);
    } else {
      CHECK(rec.osc_count == 0);
    }
    susnote_list_deinit(&susnotes);
    oevent_list_deinit(&tick_list);
    oevent_list_deinit(&engine_list);
    opstate_free(&store);
  }
}
