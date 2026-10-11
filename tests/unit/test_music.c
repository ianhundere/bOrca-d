// Unit tests for music.c, the scale and chord tables of `$` and `=` (spec
// item B2, CAP-7; spine AD-19). The tables below are written out by hand,
// independently of music.c, with the 26 first inversions spelled out. Each of
// the 62 selectors of each operator is checked through music_decode, through
// orca_run and through music_selector_name. The velocity tests at the end
// check, through orca_run, the mapping that `:`, `%` and `=` share (B5,
// CAP-10).
#include "../../gbuffer.h"
#include "../../music.h"
#include "../../opstate.h"
#include "../../sim.h"
#include "../../vmio.h"
#include "tests.h"

typedef struct {
  Glyph glyph;
  char const *name;
  Usz count;
  U8 semitones[Music_intervals_max];
} Want;

// `$` 0-9.
static Want const want_scales[] = {
    {'0', "Major", 7, {0, 2, 4, 5, 7, 9, 11}},
    {'1', "Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
    {'2', "Dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
    {'3', "Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
    {'4', "Mixolydian", 7, {0, 2, 4, 5, 7, 9, 10}},
    {'5', "Pentatonic", 5, {0, 2, 4, 7, 9}},
    {'6', "Hirajoshi", 5, {0, 2, 3, 7, 8}},
    {'7', "Iwato", 5, {0, 1, 5, 6, 10}},
    {'8', "Tetratonic", 4, {0, 4, 7, 11}},
    {'9', "Fifths", 2, {0, 7}},
};

// `=` 0-9.
static Want const want_enriched[] = {
    {'0', "Major+Oct", 4, {0, 4, 7, 12}},
    {'1', "Minor+Oct", 4, {0, 3, 7, 12}},
    {'2', "Sus4+Oct", 4, {0, 5, 7, 12}},
    {'3', "Sus2+Oct", 4, {0, 2, 7, 12}},
    {'4', "Major7+Oct3rd", 5, {0, 4, 7, 11, 16}},
    {'5', "Minor7+Oct3rd", 5, {0, 3, 7, 10, 15}},
    {'6', "Dom7+Oct5th", 5, {0, 4, 7, 10, 19}},
    {'7', "Major6+Oct", 5, {0, 4, 7, 9, 12}},
    {'8', "Minor6+Oct", 5, {0, 3, 7, 9, 12}},
    {'9', "Dim+Oct", 4, {0, 3, 6, 12}},
};

// a-z and A-Z, the same for both operators.
static Want const want_chords[] = {
    {'a', "Major", 3, {0, 4, 7}},
    {'b', "Minor", 3, {0, 3, 7}},
    {'c', "Sus4", 3, {0, 5, 7}},
    {'d', "Sus2", 3, {0, 2, 7}},
    {'e', "Major7", 4, {0, 4, 7, 11}},
    {'f', "Minor7", 4, {0, 3, 7, 10}},
    {'g', "Dom7", 4, {0, 4, 7, 10}},
    {'h', "MinorMaj7", 4, {0, 3, 7, 11}},
    {'i', "Minor6", 4, {0, 3, 7, 9}},
    {'j', "Major6", 4, {0, 4, 7, 9}},
    {'k', "Major9", 5, {0, 4, 7, 11, 14}},
    {'l', "Minor9", 5, {0, 3, 7, 10, 14}},
    {'m', "Major add9", 4, {0, 4, 7, 14}},
    {'n', "Minor add9", 4, {0, 3, 7, 14}},
    {'o', "Dim", 3, {0, 3, 6}},
    {'p', "Half Dim7", 4, {0, 3, 6, 10}},
    {'q', "Dim7", 4, {0, 3, 6, 9}},
    {'r', "Aug", 3, {0, 4, 8}},
    {'s', "Aug7", 4, {0, 4, 8, 10}},
    {'t', "Dom9", 5, {0, 4, 7, 10, 14}},
    {'u', "Dom7b9", 5, {0, 4, 7, 10, 13}},
    {'v', "Dom7#9", 5, {0, 4, 7, 10, 15}},
    {'w', "Major 6/9", 5, {0, 4, 7, 9, 14}},
    {'x', "Minor 6/9", 5, {0, 3, 7, 9, 14}},
    {'y', "Minor11", 5, {0, 3, 7, 10, 17}},
    {'z', "Minor7b5", 4, {0, 3, 6, 10}},
    {'A', "Major 1st inv", 3, {4, 7, 12}},
    {'B', "Minor 1st inv", 3, {3, 7, 12}},
    {'C', "Sus4 1st inv", 3, {5, 7, 12}},
    {'D', "Sus2 1st inv", 3, {2, 7, 12}},
    {'E', "Major7 1st inv", 4, {4, 7, 11, 12}},
    {'F', "Minor7 1st inv", 4, {3, 7, 10, 12}},
    {'G', "Dom7 1st inv", 4, {4, 7, 10, 12}},
    {'H', "MinorMaj7 1st inv", 4, {3, 7, 11, 12}},
    {'I', "Minor6 1st inv", 4, {3, 7, 9, 12}},
    {'J', "Major6 1st inv", 4, {4, 7, 9, 12}},
    {'K', "Major9 1st inv", 5, {4, 7, 11, 12, 14}},
    {'L', "Minor9 1st inv", 5, {3, 7, 10, 12, 14}},
    {'M', "Major add9 1st inv", 4, {4, 7, 12, 14}},
    {'N', "Minor add9 1st inv", 4, {3, 7, 12, 14}},
    {'O', "Dim 1st inv", 3, {3, 6, 12}},
    {'P', "Half Dim7 1st inv", 4, {3, 6, 10, 12}},
    {'Q', "Dim7 1st inv", 4, {3, 6, 9, 12}},
    {'R', "Aug 1st inv", 3, {4, 8, 12}},
    {'S', "Aug7 1st inv", 4, {4, 8, 10, 12}},
    {'T', "Dom9 1st inv", 5, {4, 7, 10, 12, 14}},
    {'U', "Dom7b9 1st inv", 5, {4, 7, 10, 12, 13}},
    {'V', "Dom7#9 1st inv", 5, {4, 7, 10, 12, 15}},
    {'W', "Major 6/9 1st inv", 5, {4, 7, 9, 12, 14}},
    {'X', "Minor 6/9 1st inv", 5, {3, 7, 9, 12, 14}},
    {'Y', "Minor11 1st inv", 5, {3, 7, 10, 12, 17}},
    {'Z', "Minor7b5 1st inv", 4, {3, 6, 10, 12}},
};

// music_decode gives count intervals, semitones[0..count), and 0 in every
// entry after them.
static bool decodes_as(Music_op op, Glyph g, Usz count, U8 const *semitones) {
  Music_intervals got;
  memset(&got, 0xAA, sizeof got);
  music_decode(op, g, &got);
  if (got.count != count)
    return false;
  for (Usz i = 0; i < Music_intervals_max; ++i) {
    U8 want = i < count ? semitones[i] : 0;
    if (got.semitones[i] != want)
      return false;
  }
  return true;
}

// music_selector_name writes name in full into a buffer of exactly
// Music_name_max bytes, the size tooltips.c uses.
static bool named(Music_op op, Glyph g, char const *name) {
  char buf[Music_name_max];
  memset(buf, 'x', sizeof buf);
  return music_selector_name(op, g, buf, sizeof buf) && strcmp(buf, name) == 0;
}

enum { Grid_w = 10 };

// Runs tick 0 of a 2 x Grid_w grid. Row 0 holds text from column 1, and row
// 1 holds below at column 1 ('*' bangs the operator above it). The grid is
// left in grid, the events in *events and, unless marks_out is NULL, the
// marks in marks_out.
static void run_cells(char const *text, Glyph below, Glyph grid[2 * Grid_w],
                      Mark *marks_out, Oevent_list *events) {
  Mark marks[2 * Grid_w];
  Usz len = strlen(text);
  CHECK(len < Grid_w);
  if (len >= Grid_w)
    len = Grid_w - 1;
  memset(grid, '.', 2 * Grid_w);
  memcpy(grid + 1, text, len);
  grid[Grid_w + 1] = below;
  memset(marks, 0, sizeof marks);
  Opstate_store store;
  opstate_init(&store);
  Orca_run_ctx const ctx = {.opstate = &store};
  oevent_list_clear(events);
  orca_run(grid, marks, 2, Grid_w, 0, events, 0, &ctx);
  opstate_free(&store);
  if (marks_out)
    memcpy(marks_out, marks, sizeof marks);
}

static char const note_names[] = "CcDdEFfGgAaB";

// Runs text, a `$` and its four inputs ("$3CA2"), and returns the note it
// writes as 12 x octave + note, or -1 when it writes no octave or no note.
// *octave_g and *note_g get the two output cells.
static int scale_out(char const *text, Glyph *octave_g, Glyph *note_g) {
  Glyph grid[2 * Grid_w];
  Oevent_list events;
  oevent_list_init(&events);
  run_cells(text, '.', grid, NULL, &events);
  CHECK(events.count == 0);
  oevent_list_deinit(&events);
  *octave_g = grid[Grid_w];     // (1, -1) from the `$` at column 1
  *note_g = grid[Grid_w + 1];   // (1, 0)
  char const *p = *note_g ? strchr(note_names, *note_g) : NULL;
  if (*octave_g < '0' || *octave_g > '9' || p == NULL)
    return -1;
  return 12 * (*octave_g - '0') + (int)(p - note_names);
}

static int scale_midi(char const *text) {
  Glyph octave_g, note_g;
  return scale_out(text, &octave_g, &note_g);
}

typedef struct {
  U8 channel, midi, velocity, duration, mono;
} Note;

enum { Notes_max = 8 };

// Bangs text, a `=` and its six inputs ("=13CA.1"), or a `:` or `%` and its
// five ("%13Cf1"), once, and writes its notes to notes in event order.
// Returns their count, or Notes_max + 1 for an event that is not a note or
// for more than Notes_max events. The entries of notes that no note filled
// are 0. Unless out_marked is NULL, *out_marked is whether the operator
// marked its own cell as an output.
static Usz bang_out(char const *text, Note notes[Notes_max],
                    bool *out_marked) {
  memset(notes, 0, Notes_max * sizeof *notes);
  Glyph grid[2 * Grid_w];
  Mark marks[2 * Grid_w];
  Oevent_list events;
  oevent_list_init(&events);
  run_cells(text, '*', grid, marks, &events);
  if (out_marked)
    *out_marked = (marks[1] & Mark_flag_output) != 0;
  Usz n = events.count > Notes_max ? Notes_max + 1 : events.count;
  for (Usz i = 0; i < events.count && i < Notes_max; ++i) {
    if (events.buffer[i].any.oevent_type != Oevent_type_midi_note) {
      n = Notes_max + 1;
      break;
    }
    Oevent_midi_note const *e = &events.buffer[i].midi_note;
    notes[i].channel = e->channel;
    notes[i].midi = (U8)(e->octave * 12 + e->note);
    notes[i].velocity = e->velocity;
    notes[i].duration = e->duration;
    notes[i].mono = e->mono;
  }
  oevent_list_deinit(&events);
  return n;
}

static Usz chord_out(char const *text, Note notes[Notes_max]) {
  return bang_out(text, notes, NULL);
}

// text plays exactly the MIDI notes want[0..count), in order.
static bool chord_plays(char const *text, Usz count, U8 const *want) {
  Note notes[Notes_max];
  if (chord_out(text, notes) != count)
    return false;
  for (Usz i = 0; i < count; ++i)
    if (notes[i].midi != want[i])
      return false;
  return true;
}

// The 62 `$` selectors: 0-9 the scales, a-z the chords in root position, A-Z
// their first inversions. Each decodes as written above, its name is written
// in full into a buffer of Music_name_max bytes, and at octave 3 on C every
// degree plays 36 + its interval; the degree after the last carries into
// octave 4.
void test_music_scale_selectors(void) {
  Usz checked = 0, bad_decode = 0, bad_name = 0, bad_note = 0;
  for (Usz t = 0; t < 2; ++t) {
    Want const *table = t == 0 ? want_scales : want_chords;
    Usz n = t == 0 ? ORCA_ARRAY_COUNTOF(want_scales)
                   : ORCA_ARRAY_COUNTOF(want_chords);
    for (Usz i = 0; i < n; ++i) {
      Want const *w = &table[i];
      ++checked;
      bad_decode += !decodes_as(Music_op_scale, w->glyph, w->count,
                                w->semitones);
      bad_name += !named(Music_op_scale, w->glyph, w->name);
      for (Usz d = 0; d <= w->count; ++d) {
        char text[] = {'$', '3', 'C', w->glyph, (char)('0' + d), '\0'};
        int want = 36 + (d < w->count ? w->semitones[d] : 12 + w->semitones[0]);
        bad_note += scale_midi(text) != want;
      }
    }
  }
  CHECK(checked == 62);
  CHECK(bad_decode == 0);
  CHECK(bad_name == 0);
  CHECK(bad_note == 0);
}

// The 62 `=` selectors: 0-9 the enriched chords, a-z the chords in root
// position, A-Z their first inversions. Each decodes as written above, its
// name is written in full into a buffer of Music_name_max bytes, and one bang
// at octave 3 on C, velocity `.`, length 1, plays 36 + each interval on
// channel 1 at velocity 127.
void test_music_midichord_selectors(void) {
  Usz checked = 0, bad_decode = 0, bad_name = 0, bad_notes = 0;
  for (Usz t = 0; t < 2; ++t) {
    Want const *table = t == 0 ? want_enriched : want_chords;
    Usz n = t == 0 ? ORCA_ARRAY_COUNTOF(want_enriched)
                   : ORCA_ARRAY_COUNTOF(want_chords);
    for (Usz i = 0; i < n; ++i) {
      Want const *w = &table[i];
      ++checked;
      bad_decode += !decodes_as(Music_op_midichord, w->glyph, w->count,
                                w->semitones);
      bad_name += !named(Music_op_midichord, w->glyph, w->name);
      char text[] = {'=', '1', '3', 'C', w->glyph, '.', '1', '\0'};
      Note notes[Notes_max];
      Usz count = chord_out(text, notes);
      bool ok = count == w->count;
      for (Usz k = 0; ok && k < count; ++k)
        ok = notes[k].midi == 36 + w->semitones[k] && notes[k].channel == 1 &&
             notes[k].velocity == 127 && notes[k].duration == 1;
      bad_notes += !ok;
    }
  }
  CHECK(checked == 62);
  CHECK(bad_decode == 0);
  CHECK(bad_name == 0);
  CHECK(bad_notes == 0);
}

// `$3CA2` and `$3CA0`, the C major first inversion E-G-C: degree 2 is C in
// octave 4, degree 0 is E. Before B2 they printed 3G and 3C.
void test_music_scale_inversion(void) {
  Glyph octave_g, note_g;
  CHECK(scale_out("$3CA2", &octave_g, &note_g) == 48);
  CHECK(octave_g == '4' && note_g == 'C');
  CHECK(scale_out("$3CA0", &octave_g, &note_g) == 40);
  CHECK(octave_g == '3' && note_g == 'E');
  // Lowercase and digit selectors are unchanged.
  CHECK(scale_out("$3Ca2", &octave_g, &note_g) == 43);
  CHECK(octave_g == '3' && note_g == 'G');
  CHECK(scale_out("$3C02", &octave_g, &note_g) == 40);
  CHECK(octave_g == '3' && note_g == 'E');
}

// `=13CAf1` plays E-G-C (40, 43, 48); `=13Caf1` still plays C-E-G. Since
// B5, velocity f is 119 on every note.
void test_music_midichord_inversion(void) {
  CHECK(chord_plays("=13CAf1", 3, (U8 const[]){40, 43, 48}));
  CHECK(chord_plays("=13Caf1", 3, (U8 const[]){36, 40, 43}));
  Note notes[Notes_max];
  CHECK(chord_out("=13CAf1", notes) == 3);
  CHECK(notes[0].velocity == 119 && notes[1].velocity == 119 &&
        notes[2].velocity == 119);
  // tests/patches/midichord_inv.orca's =13CA.1 also pins velocity 127.
  CHECK(chord_out("=13CA.1", notes) == 3);
  CHECK(notes[0].velocity == 127 && notes[2].midi == 48);
}

// The inversion's 12 goes in sorted, before any interval above 12: Major9
// (0 4 7 11 14) inverts to 4 7 11 12 14, not 4 7 11 14 12, and Minor11
// (0 3 7 10 17) to 3 7 10 12 17.
void test_music_inversion_sorts_12(void) {
  for (Usz op = 0; op < 2; ++op) {
    Music_op o = op == 0 ? Music_op_scale : Music_op_midichord;
    CHECK(decodes_as(o, 'K', 5, (U8 const[]){4, 7, 11, 12, 14}));
    CHECK(decodes_as(o, 'Y', 5, (U8 const[]){3, 7, 10, 12, 17}));
  }
  CHECK(chord_plays("=13CK.1", 5, (U8 const[]){40, 43, 47, 48, 50}));
  CHECK(chord_plays("=13CY.1", 5, (U8 const[]){39, 43, 46, 48, 53}));
  // `$` degrees 3 and 4 of K are C4 and D4.
  CHECK(scale_midi("$3CK3") == 48);
  CHECK(scale_midi("$3CK4") == 50);
}

// A glyph that is not 0-9, a-z or A-Z decodes as scale 0 for `$` and as
// enriched chord 0 for `=`, as before B2, and has no name. The cells of
// tests/patches/io_borca.orca ($pg:a, =aGA.) are included.
void test_music_other_glyphs(void) {
  static Glyph const others[] = {'*', ':', '.', '#', '=', '$', '!', '?'};
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(others); ++i) {
    Glyph g = others[i];
    CHECK(decodes_as(Music_op_scale, g, 7, want_scales[0].semitones));
    CHECK(decodes_as(Music_op_midichord, g, 4, want_enriched[0].semitones));
    char buf[32] = "unchanged";
    CHECK(!music_selector_name(Music_op_scale, g, buf, sizeof buf));
    CHECK(buf[0] == '\0');
    memcpy(buf, "unchanged", sizeof "unchanged");
    CHECK(!music_selector_name(Music_op_midichord, g, buf, sizeof buf));
    CHECK(buf[0] == '\0');
  }
  CHECK(scale_midi("$3C*2") == 40);
  CHECK(scale_midi("$3C:2") == 40);
  CHECK(chord_plays("=13C*.1", 4, (U8 const[]){36, 40, 43, 48}));
  CHECK(chord_plays("=13C..1", 4, (U8 const[]){36, 40, 43, 48}));

  // io_borca.orca's $pg:a: octave p clamps to 9, root g is G#, and degree
  // a (10) of scale 0 is a step past the octave, so the note lands in octave
  // 11 and `$` writes nothing.
  Glyph octave_g, note_g;
  CHECK(scale_out("$pg:a", &octave_g, &note_g) == -1);
  CHECK(octave_g == '.' && note_g == '.');
  // io_borca.orca's =aGA.: channel 10, octave G clamps to 9, root A (117),
  // and enriched chord 0's last note, 129, is above 127 and skipped.
  Note notes[Notes_max];
  CHECK(chord_out("=aGA...", notes) == 3);
  CHECK(notes[0].channel == 10 && notes[0].midi == 117);
  CHECK(notes[1].midi == 121 && notes[2].midi == 124);
  CHECK(notes[0].velocity == 127 && notes[0].duration == 0);
}

// `$9CA2` would land in octave 10, so `$` writes nothing, as it does above
// octave 9 today; `$9Ca2` stays in octave 9.
void test_music_octave_overflow(void) {
  Glyph octave_g, note_g;
  CHECK(scale_out("$9CA2", &octave_g, &note_g) == -1);
  CHECK(octave_g == '.' && note_g == '.');
  CHECK(scale_out("$9Ca2", &octave_g, &note_g) == 9 * 12 + 7);
}

// The names the tooltips show. `=`'s selector A reads "Chord type: Major 1st
// inv" in the TUI; tooltips.c adds the label. A buffer one byte short fails
// and is left empty.
void test_music_names(void) {
  CHECK(named(Music_op_midichord, 'A', "Major 1st inv"));
  CHECK(named(Music_op_scale, 'A', "Major 1st inv"));
  CHECK(named(Music_op_scale, '0', "Major"));
  CHECK(named(Music_op_midichord, '0', "Major+Oct"));
  CHECK(named(Music_op_midichord, 'a', "Major"));

  char buf[32];
  // "Major 1st inv" is 13 characters.
  memset(buf, 'x', sizeof buf);
  CHECK(!music_selector_name(Music_op_scale, 'A', buf, 13));
  CHECK(buf[0] == '\0');
  CHECK(music_selector_name(Music_op_scale, 'A', buf, 14));
  CHECK(strcmp(buf, "Major 1st inv") == 0);
  // "Major" is 5.
  CHECK(!music_selector_name(Music_op_scale, 'a', buf, 5));
  CHECK(music_selector_name(Music_op_scale, 'a', buf, 6));
  CHECK(strcmp(buf, "Major") == 0);
  CHECK(!music_selector_name(Music_op_scale, 'a', buf, 0));
  // The longest name. named() writes into a buffer of Music_name_max bytes,
  // tooltips.c's size, as it does for all 124 names in the selector tests.
  CHECK(named(Music_op_scale, 'N', "Minor add9 1st inv"));
}

// Shared intervals and names need no special case: p and z decode alike, as
// do P and Z and `$`'s 8 and e, and each keeps its own name; "Major" and
// "Minor" each name a scale and a chord.
void test_music_shared_intervals(void) {
  for (Usz op = 0; op < 2; ++op) {
    Music_op o = op == 0 ? Music_op_scale : Music_op_midichord;
    CHECK(decodes_as(o, 'p', 4, (U8 const[]){0, 3, 6, 10}));
    CHECK(decodes_as(o, 'z', 4, (U8 const[]){0, 3, 6, 10}));
    CHECK(decodes_as(o, 'P', 4, (U8 const[]){3, 6, 10, 12}));
    CHECK(decodes_as(o, 'Z', 4, (U8 const[]){3, 6, 10, 12}));
    CHECK(named(o, 'p', "Half Dim7") && named(o, 'z', "Minor7b5"));
    CHECK(named(o, 'P', "Half Dim7 1st inv") &&
          named(o, 'Z', "Minor7b5 1st inv"));
  }
  CHECK(decodes_as(Music_op_scale, '8', 4, (U8 const[]){0, 4, 7, 11}));
  CHECK(decodes_as(Music_op_scale, 'e', 4, (U8 const[]){0, 4, 7, 11}));
  CHECK(named(Music_op_scale, '8', "Tetratonic"));
  CHECK(named(Music_op_scale, 'e', "Major7"));
  CHECK(named(Music_op_scale, '0', "Major") &&
        named(Music_op_scale, 'a', "Major"));
  CHECK(named(Music_op_scale, '1', "Minor") &&
        named(Music_op_scale, 'b', "Minor"));
}

// Velocity (B5, CAP-10). `:`, `%` and `=` share one mapping: `.` is 127, a
// glyph worth 0 sends nothing, and any other value v gives min(v x 8 - 1,
// 127). Before B5, `=` used v x 127 / 35 and sent a glyph worth 0 at
// velocity 0.

// Bangs text once: it plays count notes, each at velocity vel and with mono
// flag mono.
static bool plays_at(char const *text, Usz count, U8 vel, U8 mono) {
  Note notes[Notes_max];
  if (bang_out(text, notes, NULL) != count)
    return false;
  for (Usz i = 0; i < count; ++i)
    if (notes[i].velocity != vel || notes[i].mono != mono)
      return false;
  return true;
}

// Velocity glyph g plays at vel on all three operators: `:13C?1` and
// `%13C?1` play one note, `%`'s with its mono flag set, and `=13Ca?1` three.
static bool velocity_everywhere(Glyph g, U8 vel) {
  char const midi[] = {':', '1', '3', 'C', g, '1', '\0'};
  char const mono[] = {'%', '1', '3', 'C', g, '1', '\0'};
  char const chord[] = {'=', '1', '3', 'C', 'a', g, '1', '\0'};
  return plays_at(midi, 1, vel, 0) && plays_at(mono, 1, vel, 1) &&
         plays_at(chord, 3, vel, 0);
}

// `=13Caf1` plays C-E-G at 119, where it played 54, and `=13C0f1` all four
// notes of enriched chord 0 at 119.
void test_music_velocity_midichord(void) {
  CHECK(chord_plays("=13Caf1", 3, (U8 const[]){36, 40, 43}));
  CHECK(plays_at("=13Caf1", 3, 119, 0));
  CHECK(chord_plays("=13C0f1", 4, (U8 const[]){36, 40, 43, 48}));
  CHECK(plays_at("=13C0f1", 4, 119, 0));
}

// The scale points, the same on `:`, `%` and `=`: `1` is 7, `7` 55, `f` and
// `F` 119, `g`, `z` and `Z` 127, and `.` 127. Every value from 1 to 35, in
// either case, gives min(v x 8 - 1, 127) on all three.
void test_music_velocity_scale_points(void) {
  static struct {
    Glyph glyph;
    U8 velocity;
  } const points[] = {{'1', 7},   {'7', 55},  {'f', 119}, {'F', 119},
                      {'g', 127}, {'z', 127}, {'Z', 127}, {'.', 127}};
  Usz bad_point = 0;
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(points); ++i)
    bad_point += !velocity_everywhere(points[i].glyph, points[i].velocity);
  CHECK(bad_point == 0);

  static char const glyphs[] =
      "123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  Usz checked = 0, bad = 0;
  for (char const *p = glyphs; *p; ++p) {
    int v = *p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10;
    int want = v * 8 - 1 > 127 ? 127 : v * 8 - 1;
    bad += !velocity_everywhere(*p, (U8)want);
    ++checked;
  }
  CHECK(checked == 61);
  CHECK(bad == 0);
}

// `:` and `%` are unchanged: `:13Cf1` and `%13Cf1` play note 36 on channel
// 1 at 119, `%` with its mono flag set.
void test_music_velocity_midi_unchanged(void) {
  Note notes[Notes_max];
  CHECK(bang_out(":13Cf1", notes, NULL) == 1);
  CHECK(notes[0].channel == 1 && notes[0].midi == 36 &&
        notes[0].velocity == 119 && notes[0].mono == 0 &&
        notes[0].duration == 1);
  CHECK(bang_out("%13Cf1", notes, NULL) == 1);
  CHECK(notes[0].channel == 1 && notes[0].midi == 36 &&
        notes[0].velocity == 119 && notes[0].mono == 1 &&
        notes[0].duration == 1);
}

// A velocity glyph worth 0, `0` or any glyph that is not 0-9, a-z or A-Z,
// sends nothing: `=` no longer sends velocity-0 note-ons, which synths read
// as note-offs. `=` still marks its own cell as an output before it checks
// for a bang. `:` and `%` return before marking theirs, as before B5, and
// `%` sends no event, so a mono note it holds keeps sounding.
void test_music_velocity_zero(void) {
  static char const *const chords[] = {"=13Ca01", "=13Ca;1", "=13Ca*1",
                                       "=13Ca#1"};
  static char const *const notes_only[] = {":13C01", ":13C;1", ":13C*1",
                                           "%13C01", "%13C;1"};
  Note notes[Notes_max];
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(chords); ++i) {
    bool marked = false;
    CHECK(bang_out(chords[i], notes, &marked) == 0);
    CHECK(marked);
  }
  for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(notes_only); ++i) {
    bool marked = true;
    CHECK(bang_out(notes_only[i], notes, &marked) == 0);
    CHECK(!marked);
  }
  // The same cells at velocity 1 play, and mark their cell.
  bool marked = false;
  CHECK(bang_out("=13Ca11", notes, &marked) == 3 && marked);
  marked = false;
  CHECK(bang_out(":13C11", notes, &marked) == 1 && marked);
  marked = false;
  CHECK(bang_out("%13C11", notes, &marked) == 1 && marked);
}
