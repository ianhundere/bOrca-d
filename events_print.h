#pragma once
#include "vmio.h"
#include <stdio.h>

// Prints one line per output event, each prefixed with "t<tick>". This file is
// shared with the upstream Orca-c comparison build, so it must compile against
// upstream's vmio.h as well: bOrca-only event types are named only inside
// #ifdef ORCA_VMIO_BORCA, which bOrca's vmio.h defines.
void events_print(FILE *stream, Usz tick, Oevent const *events, Usz count);
