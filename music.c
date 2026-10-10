// The scale and chord tables of `$` and `=` (architecture spine AD-19). See
// music.h.
#include "music.h"
#include "base.h"

typedef struct {
  char const *name;
  U8 count;
  U8 semitones[Music_intervals_max];
} Music_entry;

// 0-9 for `$`.
static Music_entry const music_scales[10] = {
    {"Major", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"Pentatonic", 5, {0, 2, 4, 7, 9}},
    {"Hirajoshi", 5, {0, 2, 3, 7, 8}},
    {"Iwato", 5, {0, 1, 5, 6, 10}},
    {"Tetratonic", 4, {0, 4, 7, 11}},
    {"Fifths", 2, {0, 7}},
};

// 0-9 for `=`: chords with an octave note added.
static Music_entry const music_enriched[10] = {
    {"Major+Oct", 4, {0, 4, 7, 12}},
    {"Minor+Oct", 4, {0, 3, 7, 12}},
    {"Sus4+Oct", 4, {0, 5, 7, 12}},
    {"Sus2+Oct", 4, {0, 2, 7, 12}},
    {"Major7+Oct3rd", 5, {0, 4, 7, 11, 16}},
    {"Minor7+Oct3rd", 5, {0, 3, 7, 10, 15}},
    {"Dom7+Oct5th", 5, {0, 4, 7, 10, 19}},
    {"Major6+Oct", 5, {0, 4, 7, 9, 12}},
    {"Minor6+Oct", 5, {0, 3, 7, 9, 12}},
    {"Dim+Oct", 4, {0, 3, 6, 12}},
};

// a-z for both operators, in root position. A-Z invert these.
static Music_entry const music_chords[26] = {
    {"Major", 3, {0, 4, 7}},              // a
    {"Minor", 3, {0, 3, 7}},              // b
    {"Sus4", 3, {0, 5, 7}},               // c
    {"Sus2", 3, {0, 2, 7}},               // d
    {"Major7", 4, {0, 4, 7, 11}},         // e
    {"Minor7", 4, {0, 3, 7, 10}},         // f
    {"Dom7", 4, {0, 4, 7, 10}},           // g
    {"MinorMaj7", 4, {0, 3, 7, 11}},      // h
    {"Minor6", 4, {0, 3, 7, 9}},          // i
    {"Major6", 4, {0, 4, 7, 9}},          // j
    {"Major9", 5, {0, 4, 7, 11, 14}},     // k
    {"Minor9", 5, {0, 3, 7, 10, 14}},     // l
    {"Major add9", 4, {0, 4, 7, 14}},     // m
    {"Minor add9", 4, {0, 3, 7, 14}},     // n
    {"Dim", 3, {0, 3, 6}},                // o
    {"Half Dim7", 4, {0, 3, 6, 10}},      // p
    {"Dim7", 4, {0, 3, 6, 9}},            // q
    {"Aug", 3, {0, 4, 8}},                // r
    {"Aug7", 4, {0, 4, 8, 10}},           // s
    {"Dom9", 5, {0, 4, 7, 10, 14}},       // t
    {"Dom7b9", 5, {0, 4, 7, 10, 13}},     // u
    {"Dom7#9", 5, {0, 4, 7, 10, 15}},     // v
    {"Major 6/9", 5, {0, 4, 7, 9, 14}},   // w
    {"Minor 6/9", 5, {0, 3, 7, 9, 14}},   // x
    {"Minor11", 5, {0, 3, 7, 10, 17}},    // y
    {"Minor7b5", 4, {0, 3, 6, 10}},       // z
};

static char const inversion_suffix[] = " 1st inv";

// The table entry for selector, and whether it is inverted (A-Z), or NULL
// for any glyph but 0-9, a-z and A-Z.
static Music_entry const *entry_of(Music_op op, Glyph selector,
                                   bool *inverted) {
  *inverted = false;
  if (selector >= 'a' && selector <= 'z')
    return &music_chords[selector - 'a'];
  if (selector >= 'A' && selector <= 'Z') {
    *inverted = true;
    return &music_chords[selector - 'A'];
  }
  if (selector < '0' || selector > '9')
    return NULL;
  int i = selector - '0';
  return op == Music_op_midichord ? &music_enriched[i] : &music_scales[i];
}

void music_decode(Music_op op, Glyph selector, Music_intervals *out) {
  bool inverted;
  Music_entry const *e = entry_of(op, selector, &inverted);
  if (!e) // any other glyph decodes as 0-9's 0
    e = entry_of(op, '0', &inverted);
  memset(out, 0, sizeof *out);
  assert(e->count >= 2 && e->count <= Music_intervals_max);
  if (!inverted) {
    out->count = e->count;
    memcpy(out->semitones, e->semitones, e->count);
    return;
  }
  // First inversion: drop the root's 0 and add the root an octave up, 12,
  // in sorted order, before any interval above 12.
  assert(e->semitones[0] == 0);
  Usz n = 0;
  bool placed = false;
  for (Usz i = 1; i < e->count; ++i) {
    if (!placed && e->semitones[i] > 12) {
      out->semitones[n++] = 12;
      placed = true;
    }
    out->semitones[n++] = e->semitones[i];
  }
  if (!placed)
    out->semitones[n++] = 12;
  out->count = (U8)n;
}

bool music_selector_name(Music_op op, Glyph selector, char *buf, Usz size) {
  if (size > 0)
    buf[0] = '\0';
  bool inverted;
  Music_entry const *e = entry_of(op, selector, &inverted);
  if (!e)
    return false;
  Usz name_len = strlen(e->name);
  Usz suffix_len = inverted ? sizeof inversion_suffix - 1 : 0;
  if (name_len + suffix_len >= size)
    return false;
  memcpy(buf, e->name, name_len);
  memcpy(buf + name_len, inversion_suffix, suffix_len);
  buf[name_len + suffix_len] = '\0';
  return true;
}
