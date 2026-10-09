// Unit tests for the per-cell operator state of &, ; and r (spec item B1,
// spine AD-4 to AD-8), run through orca_run with a caller-owned store.
#include "../../gbuffer.h"
#include "../../opstate.h"
#include "../../sim.h"
#include "../../vmio.h"
#include "tests.h"

// One tick as cli and the TUI run it: clear the marks and the event list,
// then run the VM with the store.
static void run_tick(Glyph *grid, Mark *marks, Usz height, Usz width,
                     Usz tick, Oevent_list *events, Usz seed,
                     Opstate_store *store) {
  mbuffer_clear(marks, height, width);
  oevent_list_clear(events);
  Orca_run_ctx const ctx = {.opstate = store};
  orca_run(grid, marks, height, width, tick, events, seed, &ctx);
}

// Writes text into the grid at (y, x).
static void put(Glyph *grid, Usz width, Usz y, Usz x, char const *text) {
  memcpy(grid + y * width + x, text, strlen(text));
}

// Each block of n outputs, from the first, is a permutation of
// [min, min + n). Outputs are the glyphs 0-9 and a-z.
static bool blocks_are_permutations(Glyph const *outputs, Usz count,
                                    Usz min, Usz n) {
  if (n == 0 || count % n != 0)
    return false;
  for (Usz b = 0; b < count; b += n) {
    bool seen[36] = {0};
    for (Usz i = b; i < b + n; ++i) {
      Glyph g = outputs[i];
      Usz v;
      if (g >= '0' && g <= '9')
        v = (Usz)(g - '0');
      else if (g >= 'a' && g <= 'z')
        v = (Usz)(g - 'a') + 10;
      else
        return false;
      if (v < min || v >= min + n || seen[v])
        return false;
      seen[v] = true;
    }
  }
  return true;
}

// The events of two lists match, field by field. Only note events are
// expected here; any other type counts as a mismatch.
static bool events_equal(Oevent_list const *a, Oevent_list const *b) {
  if (a->count != b->count)
    return false;
  for (Usz i = 0; i < a->count; ++i) {
    Oevent_midi_note const *p = &a->buffer[i].midi_note;
    Oevent_midi_note const *q = &b->buffer[i].midi_note;
    if (p->oevent_type != Oevent_type_midi_note ||
        q->oevent_type != Oevent_type_midi_note || p->channel != q->channel ||
        p->octave != q->octave || p->note != q->note ||
        p->velocity != q->velocity || p->duration != q->duration ||
        p->mono != q->mono)
      return false;
  }
  return true;
}

// The CAP-6 overflow fixture: a 300x80 grid with &0z12 at every 7th cell
// from cell 1, skipping the positions where the five glyphs do not fit in
// their row, so cell 4096, (13, 196), holds one. The baseline debug build
// reported a global-buffer-overflow there; with the store, 256 ticks run
// clean under ASan and UBSan, and every & outputs on every tick what a lone
// in-range &0z12 outputs. Each output lands on an empty cell (cell index
// 0 mod 7), so the copies never disturb one another.
void test_sim_bouncer_overflow_fixture(void) {
  enum { H = 80, W = 300, Ticks = 256, Ref_w = 5 };
  Glyph *grid = malloc(H * W * sizeof *grid);
  Mark *marks = malloc(H * W * sizeof *marks);
  CHECK(grid != NULL && marks != NULL);
  if (!grid || !marks) {
    free(grid);
    free(marks);
    return;
  }
  memset(grid, '.', H * W);
  Usz placed = 0;
  for (Usz c = 1; c < H * W; c += 7) {
    if (c % W + 5 > W)
      continue;
    memcpy(grid + c, "&0z12", 5);
    ++placed;
  }
  CHECK(placed == 3383);
  CHECK(grid[13 * W + 196] == '&' && 13 * W + 196 == 4096);

  Glyph ref[2 * Ref_w];
  Mark ref_marks[2 * Ref_w];
  memcpy(ref, "&0z12.....", sizeof ref);

  Opstate_store store, ref_store;
  opstate_init(&store);
  opstate_init(&ref_store);
  Oevent_list events;
  oevent_list_init(&events);
  Usz wrong = 0, checked = 0;
  bool seen[128] = {0};
  for (Usz tick = 0; tick < Ticks; ++tick) {
    run_tick(grid, marks, H, W, tick, &events, 0, &store);
    run_tick(ref, ref_marks, 2, Ref_w, tick, &events, 0, &ref_store);
    Glyph want = ref[Ref_w]; // the cell under the reference &
    seen[want & 0x7f] = true;
    for (Usz c = 1; c + W < H * W; c += 7) {
      if (c % W + 5 > W)
        continue;
      ++checked;
      wrong += grid[c + W] != want;
    }
  }
  Usz distinct = 0;
  for (Usz i = 0; i < 128; ++i)
    distinct += seen[i];
  CHECK(wrong == 0);
  CHECK(checked > 0);
  CHECK(distinct > 10); // the reference output really moves
  CHECK(store.count == placed); // one entry per &
  CHECK(events.count == 0);
  oevent_list_deinit(&events);
  opstate_free(&store);
  opstate_free(&ref_store);
  free(grid);
  free(marks);
}

// ; and r at row 70 of a 265-wide grid run as they do near the origin. The
// far ; sits at cell 18,550, (70, 0); the far r at (70, 21). Each is driven
// by the same D two rows above it as its reference copy near the origin.
// The far ; outputs the reference's glyph sequence over 32 ticks; each r
// emits one full permutation of 0-3 per 4 bangs.
void test_sim_far_cell(void) {
  enum { H = 73, W = 265, Ticks = 32 };
  Glyph *grid = malloc(H * W * sizeof *grid);
  Mark *marks = malloc(H * W * sizeof *marks);
  CHECK(grid != NULL && marks != NULL);
  if (!grid || !marks) {
    free(grid);
    free(marks);
    return;
  }
  memset(grid, '.', H * W);
  // Reference copies near the origin.
  put(grid, W, 0, 0, "D2");
  put(grid, W, 2, 0, ";23");
  put(grid, W, 0, 21, "D1");
  put(grid, W, 2, 20, "0r3");
  // Far copies, rows 68 and 70.
  put(grid, W, 68, 0, "D2");
  put(grid, W, 70, 0, ";23");
  put(grid, W, 68, 21, "D1");
  put(grid, W, 70, 20, "0r3");
  CHECK(70 * W + 0 == 18550);

  Opstate_store store;
  opstate_init(&store);
  Oevent_list events;
  oevent_list_init(&events);
  Glyph arp_ref[Ticks], arp_far[Ticks], r_ref[Ticks], r_far[Ticks];
  for (Usz tick = 0; tick < Ticks; ++tick) {
    run_tick(grid, marks, H, W, tick, &events, 0, &store);
    arp_ref[tick] = grid[3 * W + 0];
    arp_far[tick] = grid[71 * W + 0];
    r_ref[tick] = grid[3 * W + 21];
    r_far[tick] = grid[71 * W + 21];
  }
  CHECK(memcmp(arp_ref, arp_far, sizeof arp_ref) == 0);
  bool seen[128] = {0};
  Usz distinct = 0;
  for (Usz i = 0; i < Ticks; ++i) {
    distinct += !seen[arp_ref[i] & 0x7f];
    seen[arp_ref[i] & 0x7f] = true;
  }
  CHECK(distinct >= 3 && !seen['.']); // the reference really arpeggiates
  CHECK(blocks_are_permutations(r_ref, Ticks, 0, 4));
  CHECK(blocks_are_permutations(r_far, Ticks, 0, 4));
  CHECK(memcmp(r_ref, r_far, sizeof r_ref) != 0); // the cell feeds the seed
  CHECK(store.count == 4);
  oevent_list_deinit(&events);
  opstate_free(&store);
  free(grid);
  free(marks);
}

// Two r with different ranges in one patch (as tests/patches/r_shared.orca),
// banged every tick: each emits a full permutation of its range per cycle,
// and the 0-3 r emits exactly what it emits with no other r in the patch.
void test_sim_two_r_permutations(void) {
  enum { H = 4, W = 12, Ticks = 40 };
  static char const both[H * W + 1] = "..D1.....D1."
                                      "............"
                                      ".0r3....0r9."
                                      "............";
  static char const alone[H * W + 1] = "..D1........"
                                       "............"
                                       ".0r3........"
                                       "............";
  Glyph grid[H * W], grid_alone[H * W];
  Mark marks[H * W];
  memcpy(grid, both, sizeof grid);
  memcpy(grid_alone, alone, sizeof grid_alone);
  Opstate_store store, store_alone;
  opstate_init(&store);
  opstate_init(&store_alone);
  Oevent_list events;
  oevent_list_init(&events);
  Glyph small[Ticks], large[Ticks], small_alone[Ticks];
  for (Usz tick = 0; tick < Ticks; ++tick) {
    run_tick(grid, marks, H, W, tick, &events, 0, &store);
    run_tick(grid_alone, marks, H, W, tick, &events, 0, &store_alone);
    small[tick] = grid[3 * W + 2];
    large[tick] = grid[3 * W + 9];
    small_alone[tick] = grid_alone[3 * W + 2];
  }
  CHECK(blocks_are_permutations(small, Ticks, 0, 4));
  CHECK(blocks_are_permutations(large, Ticks, 0, 10));
  CHECK(memcmp(small, small_alone, sizeof small) == 0);
  // A different seed gives a different sequence, and is still a permutation.
  memcpy(grid, both, sizeof grid);
  opstate_clear(&store);
  Glyph seeded[Ticks];
  for (Usz tick = 0; tick < Ticks; ++tick) {
    run_tick(grid, marks, H, W, tick, &events, 7, &store);
    seeded[tick] = grid[3 * W + 9];
  }
  CHECK(blocks_are_permutations(seeded, Ticks, 0, 10));
  CHECK(memcmp(seeded, large, sizeof seeded) != 0);
  oevent_list_deinit(&events);
  opstate_free(&store);
  opstate_free(&store_alone);
}

// A patch with each stateful operator: a banged ; and r, a free-running &,
// and a note whose velocity is r's output, so the events depend on r's
// state too.
enum { Patch_h = 6, Patch_w = 20 };
static char const state_patch[Patch_h * Patch_w + 1] = "D1.....D1..........."
                                                       "...D1..............."
                                                       ";12...1r5...&3a21..."
                                                       "...:03C.1..........."
                                                       "...................."
                                                       "....................";

// The paused re-mark (AD-7): 100 preview passes, each on a fresh copy of
// the live store, between two real ticks leave the second tick's grid and
// events as they are with no preview. The same passes on the live store
// would change them, so the check can fail.
void test_sim_preview_isolation(void) {
  enum { H = Patch_h, W = Patch_w, Passes = 100 };
  Glyph live[H * W], control[H * W], scratch[H * W], leaky[H * W];
  Mark marks[H * W];
  memcpy(live, state_patch, sizeof live);
  Opstate_store store, control_store, scratch_store, leaky_store;
  opstate_init(&store);
  opstate_init(&control_store);
  opstate_init(&scratch_store);
  opstate_init(&leaky_store);
  Oevent_list events, control_events, preview_events, leaky_events;
  oevent_list_init(&events);
  oevent_list_init(&control_events);
  oevent_list_init(&preview_events);
  oevent_list_init(&leaky_events);

  // Tick 0, then fork a control and a leaky copy of the grid and the store.
  run_tick(live, marks, H, W, 0, &events, 0, &store);
  memcpy(control, live, sizeof control);
  memcpy(leaky, live, sizeof leaky);
  CHECK(opstate_copy(&store, &control_store));
  CHECK(opstate_copy(&store, &leaky_store));

  for (Usz i = 0; i < Passes; ++i) {
    memcpy(scratch, live, sizeof scratch);
    CHECK(opstate_copy(&store, &scratch_store));
    run_tick(scratch, marks, H, W, 1, &preview_events, 0, &scratch_store);
  }
  run_tick(live, marks, H, W, 1, &events, 0, &store);
  run_tick(control, marks, H, W, 1, &control_events, 0, &control_store);
  CHECK(memcmp(live, control, sizeof live) == 0);
  CHECK(events_equal(&events, &control_events));
  CHECK(events.count > 0);

  // The same passes straight on a store do advance it.
  for (Usz i = 0; i < Passes; ++i) {
    memcpy(scratch, leaky, sizeof scratch);
    run_tick(scratch, marks, H, W, 1, &preview_events, 0, &leaky_store);
  }
  run_tick(leaky, marks, H, W, 1, &leaky_events, 0, &leaky_store);
  CHECK(memcmp(leaky, control, sizeof leaky) != 0);

  oevent_list_deinit(&events);
  oevent_list_deinit(&control_events);
  oevent_list_deinit(&preview_events);
  oevent_list_deinit(&leaky_events);
  opstate_free(&store);
  opstate_free(&control_store);
  opstate_free(&scratch_store);
  opstate_free(&leaky_store);
}

// Clear (file open, new file, Ctrl+R): a tick after a clear equals the same
// tick run on a fresh store. A resize that prunes the store keeps the state
// of every cell inside the new bounds: the cropped grid ticks exactly as it
// does on an unpruned store.
void test_sim_clear_and_prune(void) {
  enum { H = Patch_h, W = Patch_w, Ticks = 10, Crop_w = 12 };
  Glyph grid[H * W], fresh[H * W], kept[H * W];
  Mark marks[H * W];
  memcpy(grid, state_patch, sizeof grid);
  Opstate_store store, fresh_store, kept_store;
  opstate_init(&store);
  opstate_init(&fresh_store);
  opstate_init(&kept_store);
  Oevent_list events, fresh_events, kept_events;
  oevent_list_init(&events);
  oevent_list_init(&fresh_events);
  oevent_list_init(&kept_events);
  for (Usz tick = 0; tick < Ticks; ++tick)
    run_tick(grid, marks, H, W, tick, &events, 0, &store);
  CHECK(store.count == 3);

  memcpy(fresh, grid, sizeof fresh);
  memcpy(kept, grid, sizeof kept);
  CHECK(opstate_copy(&store, &kept_store));
  Usz capacity = store.capacity;
  opstate_clear(&store);
  CHECK(store.count == 0 && store.capacity == capacity);
  run_tick(grid, marks, H, W, Ticks, &events, 0, &store);
  run_tick(fresh, marks, H, W, Ticks, &fresh_events, 0, &fresh_store);
  run_tick(kept, marks, H, W, Ticks, &kept_events, 0, &kept_store);
  CHECK(memcmp(grid, fresh, sizeof grid) == 0);
  CHECK(events_equal(&events, &fresh_events));
  CHECK(memcmp(kept, fresh, sizeof kept) != 0); // the clear mattered

  // Crop the kept grid to its first Crop_w columns, which cuts off the &,
  // and prune a copy of its store to match.
  Glyph cropped[H * Crop_w], unpruned_grid[H * Crop_w];
  Mark cropped_marks[H * Crop_w];
  gbuffer_copy_subrect(kept, cropped, H, W, H, Crop_w, 0, 0, 0, 0, H, Crop_w);
  memcpy(unpruned_grid, cropped, sizeof cropped);
  Opstate_store unpruned;
  opstate_init(&unpruned);
  CHECK(opstate_copy(&kept_store, &unpruned));
  CHECK(opstate_prune(&kept_store, H, Crop_w));
  CHECK(kept_store.count == 2 && unpruned.count == 3);
  for (Usz tick = Ticks + 1; tick < Ticks + 5; ++tick) {
    run_tick(cropped, cropped_marks, H, Crop_w, tick, &kept_events, 0,
             &kept_store);
    run_tick(unpruned_grid, cropped_marks, H, Crop_w, tick, &events, 0,
             &unpruned);
    CHECK(memcmp(cropped, unpruned_grid, sizeof cropped) == 0);
    CHECK(events_equal(&kept_events, &events));
  }
  opstate_free(&unpruned);
  oevent_list_deinit(&events);
  oevent_list_deinit(&fresh_events);
  oevent_list_deinit(&kept_events);
  opstate_free(&store);
  opstate_free(&fresh_store);
  opstate_free(&kept_store);
}
