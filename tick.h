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
//
// A note's remaining time is a count of tick bodies (spine AD-14): it is set
// to the note's duration (Oevent_midi_note.duration, 0..127) when the note
// sounds or is retriggered, and each tick body ages it by one, whatever the
// clock source. A note that sounds in tick body T is released in body
// T + duration, and durations 0 and 1 both release in body T + 1.
typedef struct {
  U8 remaining; // tick bodies left
  U16 chan_note;
} Susnote;

typedef struct {
  Susnote *buffer;
  Usz count, capacity;
} Susnote_list;

void susnote_list_init(Susnote_list *sl);
void susnote_list_deinit(Susnote_list *sl);
void susnote_list_clear(Susnote_list *sl);
// Adds notes, replacing any sustained note with the same channel and note,
// and sets [*start_removed, *end_removed) to the replaced notes, which are
// kept in the buffer past count for their note-offs. Returns false if the
// list cannot grow: then the list is unchanged and the out-params are not
// set, and the caller sends none of this batch's note-ons, so no note is left
// sounding without a sustained entry.
bool susnote_list_add_notes(Susnote_list *sl, Susnote const *restrict notes,
                            Usz count, Usz *restrict start_removed,
                            Usz *restrict end_removed);
// Ages every sustained note by one tick body. A note with 1 or fewer left is
// released, and the others lose one. Sets [*start_removed, *end_removed) to
// the released notes, kept in the buffer past count for their note-offs.
void susnote_list_advance_tick(Susnote_list *sl, Usz *restrict start_removed,
                               Usz *restrict end_removed);
void susnote_list_remove_by_chan_mask(Susnote_list *sl, Usz chan_mask,
                                      Usz *restrict start_removed,
                                      Usz *restrict end_removed);

// One tick of the sequencer, as the internal tick deadline runs it. The
// shell owns everything the pointers reach. Nothing in it is a time:
// sustained notes age by one tick per tick body, under every clock source.
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
} Tick_ctx;

// The tick body. Within a tick, the wire order is:
//   1. F8, when beat clock is on: sent by the shell, before tick_body;
//   2. every sustained note ages by one tick, and the note-offs of those
//      that run out go out;
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

// Sends a note-off for every sustained note and clears the list. With no
// sustained notes it sends nothing and touches no buffer. Used on pause, on
// quit and when the output changes.
void tick_release_all(Tick_sink const *sink, Susnote_list *susnotes);

// One tick (a sixteenth note) at bpm, in microseconds:
// (15000000 + bpm / 2) / bpm, rounded half-up and never below 1. bpm 0
// counts as 1.
U64 tick_len_us(Usz bpm);
