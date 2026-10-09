#pragma once
#include "base.h"
#include "opstate.h"
#include "vmio.h"

// What orca_run takes beyond its first seven arguments (architecture spine
// AD-4). The pointer is valid for one call and core keeps nothing between
// calls; the data it points to stays mutable. Later fields default to their
// zero value. The op-state store is the exception: it is required, and debug
// builds assert it.
typedef struct {
  Opstate_store *opstate; // the per-cell state of &, ; and r (opstate.h)
} Orca_run_ctx;

void orca_run(Glyph *restrict gbuffer, Mark *restrict mbuffer, Usz height,
              Usz width, Usz tick_number, Oevent_list *oevent_list,
              Usz random_seed, Orca_run_ctx const *ctx);

// MIDI CC Interpolation functions
void process_interpolated_midi_cc_event(Oevent_midi_cc_interpolated const *event, Usz tick_number);
void advance_midi_cc_interpolations(double delta_time, Oevent_list *oevent_list);

// BOORCH
extern Usz last_random_unique;

void midi_panic(Oevent_list *oevent_list);
