// Unit tests for the core PRNG (prng.h, spine AD-8) and the op-state store
// (opstate.h, spine AD-5 and AD-6). They use the store alone, with no grid.
#include "../../opstate.h"
#include "../../prng.h"
#include "tests.h"

// True when every payload byte of the entry is zero, as a fresh or re-tagged
// entry must be.
static bool payload_is_zero(Opstate_entry const *e) {
  static Opstate_entry const zero;
  return memcmp(&e->u, &zero.u, sizeof e->u) == 0;
}

// The generator matches the PCG reference implementation: pcg32_srandom_r
// with state 42 and stream 54 gives these first six outputs (pcg32-demo).
// Seeding by cell is deterministic and gives each cell and seed its own
// sequence, and prng_bounded stays in range.
void test_prng_pcg32_reference(void) {
  static U32 const expected[6] = {0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                  0x83d2f293, 0xbfa4784b, 0xcbed606e};
  Prng rng;
  prng_seed_raw(&rng, 42, 54);
  Usz wrong = 0;
  for (Usz i = 0; i < 6; ++i)
    wrong += prng_next_u32(&rng) != expected[i];
  CHECK(wrong == 0);

  enum { N = 8 };
  U32 a[N], b[N], other_cell[N], other_seed[N];
  Prng ra, rb, rc, rd;
  prng_seed(&ra, 0, 70, 1);
  prng_seed(&rb, 0, 70, 1);
  prng_seed(&rc, 0, 1, 70);
  prng_seed(&rd, 7, 70, 1);
  for (Usz i = 0; i < N; ++i) {
    a[i] = prng_next_u32(&ra);
    b[i] = prng_next_u32(&rb);
    other_cell[i] = prng_next_u32(&rc);
    other_seed[i] = prng_next_u32(&rd);
  }
  CHECK(memcmp(a, b, sizeof a) == 0);
  CHECK(memcmp(a, other_cell, sizeof a) != 0);
  CHECK(memcmp(a, other_seed, sizeof a) != 0);

  // Every value of a small bound comes up, and none at or above it.
  static U32 const bounds[] = {1, 2, 3, 7, 36};
  for (Usz bi = 0; bi < ORCA_ARRAY_COUNTOF(bounds); ++bi) {
    U32 bound = bounds[bi];
    bool seen[36] = {0};
    Usz out_of_range = 0;
    for (Usz i = 0; i < 2000; ++i) {
      U32 v = prng_bounded(&ra, bound);
      if (v >= bound)
        ++out_of_range;
      else
        seen[v] = true;
    }
    Usz unseen = 0;
    for (U32 v = 0; v < bound; ++v)
      unseen += !seen[v];
    CHECK(out_of_range == 0);
    CHECK(unseen == 0);
  }
  CHECK(prng_bounded(&ra, 0) == 0);
}

// Insert and read back the first and the last cell a grid can have, with no
// grid allocated: each read returns its own entry.
void test_opstate_edge_keys(void) {
  Opstate_store store;
  opstate_init(&store);
  CHECK(store.count == 0 && store.capacity == 0 && store.slots == NULL);

  Opstate_entry *e = opstate_lookup(&store, 0, 0, '&');
  CHECK(e != NULL);
  if (e) {
    CHECK(e->y == 0 && e->x == 0 && e->glyph == '&');
    CHECK(payload_is_zero(e));
    e->u.bouncer.initialized = true;
    e->u.bouncer.current_index = 11;
  }
  e = opstate_lookup(&store, 65534, 65534, ';');
  CHECK(e != NULL);
  if (e) {
    CHECK(e->y == 65534 && e->x == 65534 && e->glyph == ';');
    CHECK(payload_is_zero(e));
    e->u.arp.initialized = true;
    e->u.arp.step_counter = 22;
  }
  // The two mixed corners are cells of their own.
  e = opstate_lookup(&store, 0, 65534, 'r');
  CHECK(e != NULL && payload_is_zero(e));
  e = opstate_lookup(&store, 65534, 0, 'r');
  CHECK(e != NULL && payload_is_zero(e));
  CHECK(store.count == 4);

  e = opstate_lookup(&store, 0, 0, '&');
  CHECK(e != NULL);
  if (e)
    CHECK(e->u.bouncer.initialized && e->u.bouncer.current_index == 11);
  e = opstate_lookup(&store, 65534, 65534, ';');
  CHECK(e != NULL);
  if (e)
    CHECK(e->u.arp.initialized && e->u.arp.step_counter == 22);
  CHECK(store.count == 4);
  opstate_free(&store);
  CHECK(store.count == 0 && store.capacity == 0 && store.slots == NULL);
}

// The i-th key of the growth test: a packed block of rows 0-49, then keys
// spread over the whole key space from row 100 on. Distinct for distinct i,
// since 7919 is prime to 65000.
static void growth_key(Usz i, Usz n, Usz *y, Usz *x) {
  if (i < n / 2) {
    *y = i / 50;
    *x = i % 50;
  } else {
    *y = 100 + (i * 7919) % 65000;
    *x = (i * 104729 + 13) % 65535;
  }
}

// The table grows several times, stays a power of 2 at most half full, and
// keeps every entry. Reading entries back allocates nothing.
void test_opstate_growth(void) {
  enum { N = 5000 };
  Opstate_store store;
  opstate_init(&store);
  Usz reallocations = 0;
  Usz last_capacity = store.capacity;
  Usz missing = 0;
  for (Usz i = 0; i < N; ++i) {
    Usz y, x;
    growth_key(i, N, &y, &x);
    Opstate_entry *e = opstate_lookup(&store, y, x, '&');
    if (!e) {
      ++missing;
      continue;
    }
    e->u.bouncer.initialized = true;
    e->u.bouncer.current_index = i;
    if (store.capacity != last_capacity) {
      ++reallocations;
      last_capacity = store.capacity;
    }
  }
  CHECK(missing == 0);
  CHECK(store.count == N);
  CHECK(reallocations >= 3);
  CHECK(store.capacity >= 2 * N);
  CHECK((store.capacity & (store.capacity - 1)) == 0);

  Opstate_entry *slots = store.slots;
  Usz capacity = store.capacity;
  Usz wrong = 0;
  for (Usz i = 0; i < N; ++i) {
    Usz y, x;
    growth_key(i, N, &y, &x);
    Opstate_entry *e = opstate_lookup(&store, y, x, '&');
    wrong += !e || !e->u.bouncer.initialized ||
             e->u.bouncer.current_index != i;
  }
  CHECK(wrong == 0);
  CHECK(store.count == N);
  CHECK(store.slots == slots && store.capacity == capacity);
  opstate_free(&store);
}

// An entry that another glyph owns comes back zeroed and re-tagged; the same
// glyph gets its state back.
void test_opstate_retag(void) {
  Opstate_store store;
  opstate_init(&store);
  Opstate_entry *e = opstate_lookup(&store, 3, 4, '&');
  CHECK(e != NULL);
  if (e) {
    e->u.bouncer.initialized = true;
    e->u.bouncer.current_index = 40;
    e->u.bouncer.last_rate = 2;
    e->u.bouncer.last_shape = 5;
  }
  e = opstate_lookup(&store, 3, 4, '&');
  CHECK(e != NULL && e->u.bouncer.current_index == 40);

  e = opstate_lookup(&store, 3, 4, ';');
  CHECK(e != NULL);
  if (e) {
    CHECK(e->glyph == ';' && e->y == 3 && e->x == 4);
    CHECK(payload_is_zero(e));
    CHECK(!e->u.arp.initialized && e->u.arp.step_counter == 0 &&
          e->u.arp.last_pattern == 0 && e->u.arp.last_range == 0);
    e->u.arp.initialized = true;
    e->u.arp.step_counter = 9;
  }
  // Back to & : the bouncer restarts from a zeroed entry.
  e = opstate_lookup(&store, 3, 4, '&');
  CHECK(e != NULL && e->glyph == '&' && payload_is_zero(e));
  e = opstate_lookup(&store, 3, 4, 'r');
  CHECK(e != NULL && e->glyph == 'r' && payload_is_zero(e));
  CHECK(store.count == 1);
  opstate_free(&store);
}

// Clear empties the store and keeps its table; an entry after a clear starts
// from zero.
void test_opstate_clear_keeps_capacity(void) {
  Opstate_store store;
  opstate_init(&store);
  opstate_clear(&store); // a store that never allocated
  CHECK(store.count == 0 && store.capacity == 0);
  for (Usz i = 0; i < 100; ++i) {
    Opstate_entry *e = opstate_lookup(&store, i, i + 1, ';');
    if (e) {
      e->u.arp.initialized = true;
      e->u.arp.step_counter = i + 1;
    }
  }
  CHECK(store.count == 100);
  Opstate_entry *slots = store.slots;
  Usz capacity = store.capacity;
  opstate_clear(&store);
  CHECK(store.count == 0);
  CHECK(store.capacity == capacity && store.slots == slots);
  Usz occupied = 0;
  for (Usz i = 0; i < store.capacity; ++i)
    occupied += store.slots[i].glyph != 0;
  CHECK(occupied == 0);
  Opstate_entry *e = opstate_lookup(&store, 5, 6, ';');
  CHECK(e != NULL && payload_is_zero(e));
  CHECK(store.count == 1 && store.slots == slots);
  opstate_free(&store);
}

// Prune drops only the entries outside the new bounds, at the same capacity,
// and every entry inside keeps its state and is still found.
void test_opstate_prune(void) {
  enum { H = 20, W = 30, New_h = 12, New_w = 17 };
  static Glyph const glyphs[3] = {'&', ';', 'r'};
  Opstate_store store;
  opstate_init(&store);
  CHECK(opstate_prune(&store, 1, 1)); // an empty store
  for (Usz y = 0; y < H; ++y) {
    for (Usz x = 0; x < W; ++x) {
      Opstate_entry *e = opstate_lookup(&store, y, x, glyphs[(y + x) % 3]);
      if (e)
        e->u.bouncer.current_index = y * 100 + x + 1; // first field of each
    }
  }
  CHECK(store.count == H * W);
  Usz capacity = store.capacity;

  // Bounds that hold every entry change nothing.
  CHECK(opstate_prune(&store, H, W));
  CHECK(store.count == H * W && store.capacity == capacity);

  CHECK(opstate_prune(&store, New_h, New_w));
  CHECK(store.count == New_h * New_w);
  CHECK(store.capacity == capacity);
  Usz outside = 0;
  for (Usz i = 0; i < store.capacity; ++i) {
    Opstate_entry const *e = store.slots + i;
    outside += e->glyph != 0 && (e->y >= New_h || e->x >= New_w);
  }
  CHECK(outside == 0);

  Usz wrong = 0;
  for (Usz y = 0; y < New_h; ++y) {
    for (Usz x = 0; x < New_w; ++x) {
      Opstate_entry *e = opstate_lookup(&store, y, x, glyphs[(y + x) % 3]);
      wrong += !e || e->u.bouncer.current_index != y * 100 + x + 1;
    }
  }
  CHECK(wrong == 0);
  CHECK(store.count == New_h * New_w); // found, not added

  // A pruned cell starts from zero if it comes back.
  Opstate_entry *e = opstate_lookup(&store, New_h, 0, glyphs[New_h % 3]);
  CHECK(e != NULL && payload_is_zero(e));
  CHECK(store.count == New_h * New_w + 1);

  // Bounds that hold nothing empty the store.
  CHECK(opstate_prune(&store, 0, 0));
  CHECK(store.count == 0 && store.capacity == capacity);
  opstate_free(&store);
}

// Copy reproduces the source, whatever the destination held, and the two
// stores are independent afterwards.
void test_opstate_copy(void) {
  Opstate_store src, dest;
  opstate_init(&src);
  opstate_init(&dest);
  CHECK(opstate_copy(&src, &dest)); // empty into empty
  CHECK(dest.count == 0);

  for (Usz i = 0; i < 10; ++i) {
    Opstate_entry *e = opstate_lookup(&src, i, 2 * i, 'r');
    if (e) {
      e->u.random.initialized = true;
      e->u.random.current_index = (U8)i;
    }
  }
  CHECK(opstate_copy(&src, &dest));
  CHECK(dest.count == src.count && dest.capacity == src.capacity);
  CHECK(dest.slots != src.slots);
  CHECK(dest.slots && src.slots &&
        memcmp(dest.slots, src.slots, src.capacity * sizeof *src.slots) == 0);

  // Changing the copy leaves the source alone.
  Opstate_entry *e = opstate_lookup(&dest, 3, 6, 'r');
  if (e)
    e->u.random.current_index = 99;
  opstate_lookup(&dest, 40, 40, '&');
  e = opstate_lookup(&src, 3, 6, 'r');
  CHECK(e != NULL && e->u.random.current_index == 3);
  CHECK(src.count == 10);

  // A source that has grown past the copy's capacity.
  for (Usz i = 0; i < 200; ++i)
    opstate_lookup(&src, 100 + i, 7, ';');
  CHECK(src.capacity > dest.capacity);
  CHECK(opstate_copy(&src, &dest));
  CHECK(dest.count == src.count && dest.capacity == src.capacity);
  CHECK(dest.slots && src.slots &&
        memcmp(dest.slots, src.slots, src.capacity * sizeof *src.slots) == 0);

  // A copy whose table has grown past a small source's takes the source's
  // capacity and holds only the source's entries.
  for (Usz i = 0; i < 200; ++i)
    opstate_lookup(&dest, 500 + i, 9, '&');
  Opstate_store small;
  opstate_init(&small);
  for (Usz i = 0; i < 5; ++i) {
    e = opstate_lookup(&small, 20 + i, 30 + i, ';');
    if (e) {
      e->u.arp.initialized = true;
      e->u.arp.step_counter = 100 + i;
    }
  }
  CHECK(dest.capacity > small.capacity);
  CHECK(opstate_copy(&small, &dest));
  CHECK(dest.capacity == small.capacity && dest.count == small.count);
  Usz occupied = 0;
  for (Usz i = 0; i < dest.capacity; ++i)
    occupied += dest.slots[i].glyph != 0;
  CHECK(occupied == small.count); // no stale entry
  Usz wrong = 0;
  for (Usz i = 0; i < 5; ++i) {
    e = opstate_lookup(&dest, 20 + i, 30 + i, ';');
    wrong += !e || !e->u.arp.initialized || e->u.arp.step_counter != 100 + i;
  }
  CHECK(wrong == 0);
  CHECK(dest.count == small.count); // found, not added
  opstate_free(&small);

  // An empty source empties the copy.
  opstate_clear(&src);
  CHECK(opstate_copy(&src, &dest));
  CHECK(dest.count == 0);
  e = opstate_lookup(&dest, 3, 6, 'r');
  CHECK(e != NULL && payload_is_zero(e));
  opstate_free(&src);
  opstate_free(&dest);
}

// Reset anchors zeroes the phase of every & and touches nothing else.
void test_opstate_reset_anchors(void) {
  Opstate_store store;
  opstate_init(&store);
  opstate_reset_anchors(&store); // a store that never allocated
  Opstate_entry *e = opstate_lookup(&store, 1, 1, '&');
  if (e) {
    e->u.bouncer.initialized = true;
    e->u.bouncer.current_index = 50;
    e->u.bouncer.last_rate = 3;
    e->u.bouncer.last_shape = 2;
  }
  e = opstate_lookup(&store, 2, 2, ';');
  if (e) {
    e->u.arp.initialized = true;
    e->u.arp.step_counter = 9;
  }
  e = opstate_lookup(&store, 3, 3, 'r');
  if (e) {
    e->u.random.initialized = true;
    e->u.random.current_index = 2;
  }
  opstate_reset_anchors(&store);
  e = opstate_lookup(&store, 1, 1, '&');
  CHECK(e != NULL);
  if (e)
    CHECK(e->u.bouncer.current_index == 0 && e->u.bouncer.initialized &&
          e->u.bouncer.last_rate == 3 && e->u.bouncer.last_shape == 2);
  e = opstate_lookup(&store, 2, 2, ';');
  CHECK(e != NULL && e->u.arp.initialized && e->u.arp.step_counter == 9);
  e = opstate_lookup(&store, 3, 3, 'r');
  CHECK(e != NULL && e->u.random.initialized &&
        e->u.random.current_index == 2);
  CHECK(store.count == 3);
  opstate_free(&store);
}
