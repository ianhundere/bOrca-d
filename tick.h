#pragma once
#include "base.h"
#include "opstate.h"
#include "vmio.h"

// The tick path (architecture spine AD-12, AD-14). tick.c is shell code that
// obeys the core include allow-list (AD-2): it reads no clock, does no I/O,
// keeps no writable globals of its own and sends only through the sink below,
// so the unit tests link it against libc alone. Until B3, though, tick_body
// and the event send change sim.c's global glide table: the engine run
// advances it, and each CCI the VM emits registers a glide in it.

// The send sink, provided by the shell, which also sends its own single bytes
// through it. u is the shell's context, passed back on every call.
//   midi3: every three-byte MIDI message, note-offs included;
//   midi1: single bytes (the shell's F8, FA, FC);
//   osc:   OSC ints and UDP strings (Oevent_type_osc_ints, _udp_string).
// Note-offs are never Oevents.
typedef struct {
  void *u;
  void (*midi3)(void *u, int status, int d1, int d2);
  void (*midi1)(void *u, int byte);
  void (*osc)(void *u, Oevent const *e);
} Tick_sink;

// Susnote is for handling MIDI note sustains -- each MIDI on event should be
// matched with a MIDI note-off event. The duration/sustain length of a MIDI
// note is specified when it is first triggered, so the orca VM itself is not
// responsible for sending the note-off event. We keep a list of currently 'on'
// notes so that they can have a matching 'off' sent at the correct time.
typedef struct {
  float remaining;
  U16 chan_note;
} Susnote;

typedef struct {
  Susnote *buffer;
  Usz count, capacity;
} Susnote_list;

void susnote_list_init(Susnote_list *sl);
void susnote_list_deinit(Susnote_list *sl);
void susnote_list_clear(Susnote_list *sl);
void susnote_list_add_notes(Susnote_list *sl, Susnote const *restrict notes,
                            Usz count, Usz *restrict start_removed,
                            Usz *restrict end_removed);
void susnote_list_advance_time(
    Susnote_list *sl, double delta_time, Usz *restrict start_removed,
    Usz *restrict end_removed,
    // 1.0 if no notes remain or none are shorter than 1.0
    double *soonest_deadline);
void susnote_list_remove_by_chan_mask(Susnote_list *sl, Usz chan_mask,
                                      Usz *restrict start_removed,
                                      Usz *restrict end_removed);

// Returns 1.0 if no notes remain or none are shorter than 1.0
double susnote_list_soonest_deadline(Susnote_list const *sl);

// One tick of the sequencer, as the internal tick deadline runs it. The
// shell owns everything the pointers reach. The doubles are seconds and
// belong to B4's note aging, which B8 replaces with tick counts.
typedef struct {
  Glyph *gbuffer;
  Mark *mbuffer;
  Usz height, width;
  Usz random_seed;
  Opstate_store *opstate;   // the live store
  Usz *tick_num;            // incremented once per tick body
  Oevent_list *tick_list;   // the VM's events, then sent
  Oevent_list *engine_list; // the glide engine's output: cleared, then sent
  Susnote_list *susnotes;
  double age_secs;       // how far sustained notes age per tick body
  double frame_secs;     // seconds per unit of note length
  double *next_note_off; // the soonest sustained-note deadline, in seconds
} Tick_ctx;

// The tick body. Within a tick, the wire order is:
//   1. F8, when beat clock is on: sent by the shell, before tick_body;
//   2. the note-offs of sustained notes that age out;
//   3. the glide engine's output: the engine list is cleared, the engine
//      runs into it, and its CCs go out;
//   then the VM runs into the tick list, ++*tick_num, and the tick list goes
//   out, as the engine list did:
//   4. CCs, pitch bends and OSC/UDP, at once, in list (VM) order; a CCI
//      registers a glide instead, for step 3 of later ticks;
//   5. the note-offs of retriggered notes (a new note on a sustained channel
//      and note), then the note-ons;
//   6. the mono round: the note-offs of every sustained note on a channel
//      with a new mono note, then the mono note-ons.
// The shell sends nothing else for the tick.
void tick_body(Tick_ctx const *ctx, Tick_sink const *sink);

// Clears the marks and the list, then runs the VM once into the list. Sends
// nothing and ages nothing: the paused re-mark (the preview, on the scratch
// grid and store) and step-forward use it as well as tick_body.
void tick_run_vm(Glyph *restrict gbuffer, Mark *restrict mbuffer, Usz height,
                 Usz width, Usz tick_num, Oevent_list *list, Usz random_seed,
                 Opstate_store *opstate);

// Sends a note-off for every sustained note, clears the list and sets
// *next_note_off to 1.0. Used on pause, on quit and when the output changes.
void tick_release_all(Tick_sink const *sink, Susnote_list *susnotes,
                      double *next_note_off);

// One tick (a sixteenth note) at bpm, in microseconds:
// (15000000 + bpm / 2) / bpm, rounded half-up and never below 1. bpm 0
// counts as 1.
U64 tick_len_us(Usz bpm);
