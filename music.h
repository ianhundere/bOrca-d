#pragma once
#include "base.h"

// The scale and chord tables of `$` and `=` (architecture spine AD-19): every
// selector's name and intervals, held once. sim.c decodes both operators'
// selectors through music_decode, and tooltips.c names them through
// music_selector_name. It is core: it does no I/O and keeps no writable
// globals, and every table is const at every level.
//
// A selector is decoded from its raw glyph, so case matters:
//   0-9   the scales for `$`, the enriched chords for `=`;
//   a-z   the 26 chords in root position, the same for both operators;
//   A-Z   the first inversion of the same chord, built at decode time from
//         root position: drop the 0, add 12 and keep the list ascending, so
//         the 12 goes in before any interval above 12 and the note count
//         does not change. Major9, 0 4 7 11 14, inverts to 4 7 11 12 14;
//   other any other glyph decodes as 0-9's 0: scale 0, or enriched chord 0.

// Which operator reads the selector. It decides what 0-9 mean.
typedef enum {
  Music_op_scale,     // `$`: 0-9 are scales
  Music_op_midichord, // `=`: 0-9 are enriched chords
} Music_op;

// The most intervals a selector has: 7, for the 7-note scales. Chords have
// 3 to 5.
enum { Music_intervals_max = 7 };

// A buffer of Music_name_max bytes holds every selector's name, " 1st inv"
// and the NUL included. The longest, "Minor add9 1st inv", needs 19.
enum { Music_name_max = 32 };

// A decoded selector: count semitone offsets above the root, ascending. The
// entries from count on are 0.
typedef struct {
  U8 count;
  U8 semitones[Music_intervals_max];
} Music_intervals;

// Decodes selector for op into *out. Every glyph decodes: one that is not
// 0-9, a-z or A-Z gives 0-9's 0. out->count is at least 2.
void music_decode(Music_op op, Glyph selector, Music_intervals *out);

// Writes the selector's name, NUL-terminated, into buf: the scale or
// enriched-chord name for 0-9 (by op), the chord name for a-z, and that name
// followed by " 1st inv" for A-Z, as in "Major 1st inv". Returns false, and
// writes an empty string when size is not 0, for any other glyph or when buf
// cannot hold the name and its NUL.
bool music_selector_name(Music_op op, Glyph selector, char *buf, Usz size);
