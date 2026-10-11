// readme-gen: prints the Markdown that sits between README.md's
// <!-- tables:begin --> and <!-- tables:end --> markers, built from music.h
// (architecture spine AD-19). tests/check-doc-sync.sh diffs this output
// against the README, so the tables cannot drift from what `$` and `=` play.
//
//   ./tool build readme-gen && build/readme-gen
//
// It reads the selectors only through music_selector_name and music_decode,
// and copies no table. Exit status: 0, or 1 when a selector has no name, a
// semitone has no spelling, or stdout cannot be written.
#include "../base.h"
#include "../music.h"
#include <stdio.h>

// The note name of each semitone above a C root, one map per table. Both
// spell 0-11 as C Db D Eb E F Gb G G# A Bb B and continue an octave up. They
// differ only at 15: the chord table's Dom7#9 has a sharp ninth, D#, and the
// enriched table's Minor7+Oct3rd an octave minor third, Eb.
static char const *const chord_spelling[] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "G#", "A", "Bb", "B", // 0-11
    "C", "Db", "D", "D#", "E", "F"};                                  // 12-17
static char const *const enriched_spelling[] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "G#", "A", "Bb", "B", // 0-11
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G"};                       // 12-19

// Prints iv's notes joined by '-', spelled from spelling. Exits 1 when a
// semitone falls outside the map.
static void print_notes(char const *const *spelling, Usz n_spelling,
                        Music_intervals const *iv) {
  for (Usz i = 0; i < iv->count; ++i) {
    Usz s = iv->semitones[i];
    if (s >= n_spelling) {
      fprintf(stderr, "readme-gen: no spelling for semitone %zu\n", s);
      exit(1);
    }
    printf("%s%s", i ? "-" : "", spelling[s]);
  }
}

// Writes selector g's name for op into buf (Music_name_max bytes). Exits 1
// when music.h has no name for it.
static void name_of(Music_op op, Glyph g, char *buf) {
  if (!music_selector_name(op, g, buf, Music_name_max)) {
    fprintf(stderr, "readme-gen: no name for selector %c\n", g);
    exit(1);
  }
}

int main(void) {
  char name[Music_name_max];
  Music_intervals iv;

  puts("### Available Scales (0-9, `$` only):");
  puts("| Value | Scale Type |");
  puts("|:-----:|------------|");
  for (Glyph g = '0'; g <= '9'; ++g) {
    name_of(Music_op_scale, g, name);
    printf("|   %c   | %s |\n", g, name);
  }
  puts("");

  // a-z are the same chords for `$` and `=`; A-Z are their first inversions.
  puts("### Available Chords (a-z = root, A-Z = first inversion):");
  puts("| Value | Chord Type | Root Notes | First Inversion |");
  puts("|:-----:|------------|:----------:|:---------------:|");
  for (Glyph g = 'a'; g <= 'z'; ++g) {
    Glyph up = (Glyph)(g - 'a' + 'A');
    name_of(Music_op_midichord, g, name);
    printf("|   %c/%c   | %s | ", g, up, name);
    music_decode(Music_op_midichord, g, &iv);
    print_notes(chord_spelling, ORCA_ARRAY_COUNTOF(chord_spelling), &iv);
    printf(" | ");
    music_decode(Music_op_midichord, up, &iv);
    print_notes(chord_spelling, ORCA_ARRAY_COUNTOF(chord_spelling), &iv);
    puts(" |");
  }
  puts("");

  puts("### Enriched Chords (0-9, `=` only):");
  puts("| Index | Name | Intervals | Notes (C root) |");
  puts("|:-----:|:----:|:---------:|:--------------:|");
  for (Glyph g = '0'; g <= '9'; ++g) {
    name_of(Music_op_midichord, g, name);
    music_decode(Music_op_midichord, g, &iv);
    printf("| %c | %s | ", g, name);
    for (Usz i = 0; i < iv.count; ++i)
      printf("%s%u", i ? "," : "", (unsigned)iv.semitones[i]);
    printf(" | ");
    print_notes(enriched_spelling, ORCA_ARRAY_COUNTOF(enriched_spelling), &iv);
    puts(" |");
  }

  // The output is what doc-sync compares, so a write failure must not exit 0.
  if (fflush(stdout) != 0 || ferror(stdout)) {
    perror("readme-gen: stdout");
    return 1;
  }
  return 0;
}
