#include "opstate.h"

// A 32-bit integer hash (lowbias32) of the cell. y and x fit in 16 bits each
// (ORCA_Y_MAX, ORCA_X_MAX), so the packed key is unique per cell.
static Usz opstate_hash(Usz y, Usz x) {
  U32 k = ((U32)y << 16) | (U32)x;
  k ^= k >> 16;
  k *= UINT32_C(0x7feb352d);
  k ^= k >> 15;
  k *= UINT32_C(0x846ca68b);
  k ^= k >> 16;
  return (Usz)k;
}

// The first empty slot on the probe path of (y, x). The table must hold an
// empty slot, which the half-full limit guarantees.
static Opstate_entry *opstate_empty_slot(Opstate_entry *slots, Usz capacity,
                                         Usz y, Usz x) {
  Usz mask = capacity - 1;
  Usz i = opstate_hash(y, x) & mask;
  while (slots[i].glyph != 0)
    i = (i + 1) & mask;
  return slots + i;
}

// Moves every entry into a new zeroed table of new_capacity, keeping only
// the cells inside height x width. False, with the store unchanged, if the
// allocation fails.
static bool opstate_rebuild(Opstate_store *store, Usz new_capacity,
                            Usz height, Usz width) {
  Opstate_entry *slots = calloc(new_capacity, sizeof *slots);
  if (!slots)
    return false;
  Usz count = 0;
  for (Usz i = 0; i < store->capacity; ++i) {
    Opstate_entry const *e = store->slots + i;
    if (e->glyph == 0 || (Usz)e->y >= height || (Usz)e->x >= width)
      continue;
    *opstate_empty_slot(slots, new_capacity, e->y, e->x) = *e;
    ++count;
  }
  free(store->slots);
  store->slots = slots;
  store->count = count;
  store->capacity = new_capacity;
  return true;
}

void opstate_init(Opstate_store *store) {
  store->slots = NULL;
  store->count = 0;
  store->capacity = 0;
}

void opstate_free(Opstate_store *store) {
  free(store->slots);
  opstate_init(store);
}

Opstate_entry *opstate_lookup(Opstate_store *store, Usz y, Usz x, Glyph glyph) {
  assert(glyph != 0);
  assert(y <= ORCA_Y_MAX && x <= ORCA_X_MAX);
  if (store->capacity > 0) {
    Usz mask = store->capacity - 1;
    for (Usz i = opstate_hash(y, x) & mask;; i = (i + 1) & mask) {
      Opstate_entry *e = store->slots + i;
      if (e->glyph == 0)
        break;
      if ((Usz)e->y == y && (Usz)e->x == x) {
        if (e->glyph != glyph) {
          memset(&e->u, 0, sizeof e->u);
          e->glyph = glyph;
        }
        return e;
      }
    }
  }
  // Not found: add an entry, keeping the table at most half full. The
  // capacity doubles, as Oevent_list's does (vmio.c).
  Usz need = (store->count + 1) * 2;
  if (need > store->capacity) {
    Usz new_capacity = need < 16 ? 16 : orca_round_up_power2(need);
    if (!opstate_rebuild(store, new_capacity, (Usz)ORCA_Y_MAX + 1,
                         (Usz)ORCA_X_MAX + 1))
      return NULL;
  }
  Opstate_entry *e = opstate_empty_slot(store->slots, store->capacity, y, x);
  memset(e, 0, sizeof *e);
  e->y = (U16)y;
  e->x = (U16)x;
  e->glyph = glyph;
  ++store->count;
  return e;
}

void opstate_clear(Opstate_store *store) {
  if (store->slots)
    memset(store->slots, 0, store->capacity * sizeof *store->slots);
  store->count = 0;
}

bool opstate_prune(Opstate_store *store, Usz height, Usz width) {
  Usz outside = 0;
  for (Usz i = 0; i < store->capacity; ++i) {
    Opstate_entry const *e = store->slots + i;
    if (e->glyph != 0 && ((Usz)e->y >= height || (Usz)e->x >= width))
      ++outside;
  }
  if (outside == 0)
    return true;
  if (outside == store->count) {
    opstate_clear(store);
    return true;
  }
  return opstate_rebuild(store, store->capacity, height, width);
}

bool opstate_copy(Opstate_store const *src, Opstate_store *dest) {
  if (src == dest)
    return true;
  if (src->count == 0) {
    opstate_clear(dest);
    return true;
  }
  // The slot of an entry depends on the capacity, so dest takes src's.
  if (dest->capacity != src->capacity) {
    Opstate_entry *slots =
        realloc(dest->slots, src->capacity * sizeof *dest->slots);
    if (!slots) {
      opstate_clear(dest);
      return false;
    }
    dest->slots = slots;
    dest->capacity = src->capacity;
  }
  memcpy(dest->slots, src->slots, src->capacity * sizeof *dest->slots);
  dest->count = src->count;
  return true;
}

void opstate_reset_anchors(Opstate_store *store) {
  for (Usz i = 0; i < store->capacity; ++i) {
    Opstate_entry *e = store->slots + i;
    if (e->glyph == '&')
      e->u.bouncer.current_index = 0;
  }
}
