// Unit tests for the CORE modules. They link CORE against libc only.
#include "../../gbuffer.h"
#include "../../sim.h"
#include "../../vmio.h"
#include "tests.h"

// The relative peek and poke ignore cells outside the grid: a peek returns
// '.', a poke writes nothing, and neither wraps from the end of one row to
// the start of the next.
void test_gbuffer_bounds(void) {
  enum { H = 3, W = 4, Guard = 4 };
  // The grid sits between two guard runs that catch a stray write.
  Glyph mem[Guard + H * W + Guard];
  Glyph *grid = mem + Guard;
  memset(mem, '#', sizeof mem);
  for (Usz i = 0; i < H * W; ++i)
    grid[i] = (Glyph)('a' + i);

  // In bounds: (1, 1) + (1, 2) is (2, 3), the last cell.
  CHECK(gbuffer_peek_relative(grid, H, W, 1, 1, 1, 2) == 'a' + 11);
  // Out of bounds on each side.
  CHECK(gbuffer_peek_relative(grid, H, W, 0, 0, -1, 0) == '.');
  CHECK(gbuffer_peek_relative(grid, H, W, 0, 0, 0, -1) == '.');
  CHECK(gbuffer_peek_relative(grid, H, W, H - 1, 0, 1, 0) == '.');
  CHECK(gbuffer_peek_relative(grid, H, W, 0, W - 1, 0, 1) == '.');
  // Far out of bounds, and past the end of a row instead of wrapping.
  CHECK(gbuffer_peek_relative(grid, H, W, 0, 0, -1000, 1000) == '.');
  CHECK(gbuffer_peek_relative(grid, H, W, 1, W - 1, 0, 1) == '.');

  gbuffer_poke_relative(grid, H, W, 0, 0, -1, 0, 'X');
  gbuffer_poke_relative(grid, H, W, 0, 0, 0, -1, 'X');
  gbuffer_poke_relative(grid, H, W, H - 1, 0, 1, 0, 'X');
  gbuffer_poke_relative(grid, H, W, 0, W - 1, 0, 1, 'X');
  gbuffer_poke_relative(grid, H, W, 1, W - 1, 0, 1, 'X');
  gbuffer_poke_relative(grid, H, W, 0, 0, -1000, 1000, 'X');
  Usz changed = 0;
  for (Usz i = 0; i < H * W; ++i)
    changed += grid[i] != (Glyph)('a' + i);
  CHECK(changed == 0);

  // In bounds, the relative and absolute pokes write the one cell.
  gbuffer_poke_relative(grid, H, W, 0, 0, 2, 3, 'Y');
  CHECK(grid[2 * W + 3] == 'Y');
  gbuffer_poke(grid, H, W, 1, 2, 'Z');
  CHECK(grid[1 * W + 2] == 'Z');

  Usz guards_hit = 0;
  for (Usz i = 0; i < Guard; ++i) {
    guards_hit += mem[i] != '#';
    guards_hit += mem[Guard + H * W + i] != '#';
  }
  CHECK(guards_hit == 0);
}

// Growing past the initial capacity and several reallocations keeps every
// item, in order. Copy and clear keep them too.
void test_oevent_list_growth(void) {
  enum { N = 300 };
  Oevent_list list;
  oevent_list_init(&list);
  CHECK(list.count == 0);
  Usz reallocations = 0;
  Usz last_capacity = list.capacity;
  for (Usz i = 0; i < N; ++i) {
    Oevent *e = oevent_list_alloc_item(&list);
    memset(e, 0, sizeof *e);
    e->midi_note.oevent_type = Oevent_type_midi_note;
    e->midi_note.channel = (U8)(i % 16);
    e->midi_note.note = (U8)(i % 128);
    e->midi_note.velocity = (U8)(i / 128);
    if (list.capacity != last_capacity) {
      ++reallocations;
      last_capacity = list.capacity;
    }
  }
  CHECK(list.count == N);
  CHECK(list.capacity >= N);
  CHECK(reallocations >= 3);
  // Read only the items the list holds, so a growth bug fails the checks
  // instead of ending the run in an ASan abort.
  Usz held = list.count < N ? list.count : N;
  Usz wrong = 0;
  for (Usz i = 0; i < held; ++i) {
    Oevent_midi_note const *n = &list.buffer[i].midi_note;
    wrong += n->oevent_type != Oevent_type_midi_note ||
             n->channel != i % 16 || n->note != i % 128 ||
             n->velocity != i / 128;
  }
  CHECK(wrong == 0);

  Oevent_list copy;
  oevent_list_init(&copy);
  oevent_list_copy(&list, &copy);
  CHECK(copy.count == list.count);
  // A count mismatch is already recorded above; compare only equal counts.
  if (copy.count == list.count && list.count == N)
    CHECK(copy.buffer != NULL && list.buffer != NULL &&
          memcmp(copy.buffer, list.buffer, N * sizeof(Oevent)) == 0);

  Usz capacity = list.capacity;
  oevent_list_clear(&list);
  CHECK(list.count == 0);
  CHECK(list.capacity == capacity);
  oevent_list_deinit(&copy);
  oevent_list_deinit(&list);
}

// One tick of a stable operator: A adds its two inputs and writes the sum
// below itself, and emits no event.
void test_orca_run_smoke(void) {
  enum { H = 3, W = 3 };
  Glyph grid[H * W];
  Mark marks[H * W];
  memcpy(grid, "1A2"
               "..."
               "...",
         sizeof grid);
  memset(marks, 0, sizeof marks);
  Oevent_list events;
  oevent_list_init(&events);
  orca_run(grid, marks, H, W, 0, &events, 0);
  CHECK(grid[1 * W + 1] == '3');
  CHECK(memcmp(grid, "1A2", 3) == 0);
  CHECK(events.count == 0);
  oevent_list_deinit(&events);
}
