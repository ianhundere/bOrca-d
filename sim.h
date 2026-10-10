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
