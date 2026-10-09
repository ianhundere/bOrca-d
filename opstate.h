#pragma once
#include "base.h"
#include "prng.h"

// The op-state store (architecture spine AD-5, AD-6): the per-cell state of
// the stateful operators &, ; and lowercase r, in one table that the caller
// owns and passes to orca_run through Orca_run_ctx (sim.h). The TUI owns a
// live store and a scratch store for the paused preview; cli owns one.
//
// An entry is keyed by its cell (y, x) and records the raw glyph of the
// operator that owns it. That glyph tags the payload union: '&' uses
// .bouncer, ';' uses .arp and 'r' uses .random. A lookup by any other glyph
// hands the operator a zeroed entry, so an edited cell never inherits another
// operator's state. A zeroed payload is the operator's first visit: its
// `initialized` flag is false, and the operator seeds its own fields.
//
// Open addressing with linear probing over a power-of-2 table that is at most
// half full. Entries are never removed one at a time, so there are no
// tombstones: clear empties the table and keeps its capacity, and prune
// rebuilds it. The table grows only when a lookup adds an entry, amortized
// like Oevent_list, so ticks that add no new stateful cell never allocate.
//
// Only the shell calls the lifecycle operations (init, free, clear, prune,
// copy, reset anchors); operators call only opstate_lookup.

typedef struct {
  bool initialized;
  Usz current_index; // phase: position in the 128-step waveform
  Usz last_rate;
  Usz last_shape;
} Opstate_bouncer; // '&'

typedef struct {
  bool initialized;
  Usz step_counter;
  Usz last_pattern;
  Usz last_range;
} Opstate_arp; // ';'

enum { Opstate_random_max_size = 36 }; // the values 0-9 and a-z

typedef struct {
  bool initialized; // the generator is seeded and the bag is filled
  U8 current_index; // next position in the bag
  U8 sequence_size;
  U8 last_min, last_max;
  U8 sequence[Opstate_random_max_size]; // the shuffled bag
  Prng prng;
} Opstate_random; // 'r'

typedef struct {
  U16 y, x;
  Glyph glyph; // owner; 0 marks an empty slot
  union {
    Opstate_bouncer bouncer;
    Opstate_arp arp;
    Opstate_random random;
  } u;
} Opstate_entry;

typedef struct {
  Opstate_entry *slots;
  Usz count, capacity; // capacity is 0 or a power of 2
} Opstate_store;

void opstate_init(Opstate_store *store);
// Frees the table, not the store itself. The store is empty afterwards and
// can be used again.
void opstate_free(Opstate_store *store);

// The entry of the cell (y, x) for the operator glyph, which must not be 0.
// The cell must lie within ORCA_Y_MAX x ORCA_X_MAX. If no entry exists, or
// one owned by another glyph does, the result is a zeroed payload tagged with
// glyph. The pointer is valid until the next lookup or lifecycle call. NULL
// only when growing the table fails; the store is then unchanged.
Opstate_entry *opstate_lookup(Opstate_store *store, Usz y, Usz x, Glyph glyph);

// Removes every entry and keeps the capacity.
void opstate_clear(Opstate_store *store);

// Removes the entries whose cell lies outside height x width, and rebuilds
// the table at the same capacity. The entries inside keep their state. False
// if the rebuild cannot allocate; the store is then unchanged.
bool opstate_prune(Opstate_store *store, Usz height, Usz width);

// Overwrites dest with a copy of src, reusing dest's table when the capacity
// already matches. False if dest cannot be resized; dest is then empty.
bool opstate_copy(Opstate_store const *src, Opstate_store *dest);

// Zeroes the phase of every & entry, so each restarts its waveform on its
// next tick as a bang would make it. The other fields and operators keep
// their state.
void opstate_reset_anchors(Opstate_store *store);
