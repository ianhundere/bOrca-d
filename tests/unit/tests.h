// The registry of unit tests in build/unit_tests (./tool build test). See
// tests/README.md for how to add a test.
#pragma once
#include "check.h"

// X-macro lists, in run order. X(name) registers the function test_<name>.
//
// CORE_TESTS: tests that link CORE against libc only.
#define CORE_TESTS(X)                                                          \
  X(gbuffer_bounds)                                                            \
  X(oevent_list_growth)                                                        \
  X(orca_run_smoke)                                                            \
  X(prng_pcg32_reference)                                                      \
  X(opstate_edge_keys)                                                         \
  X(opstate_growth)                                                            \
  X(opstate_retag)                                                             \
  X(opstate_clear_keeps_capacity)                                              \
  X(opstate_prune)                                                             \
  X(opstate_copy)                                                              \
  X(opstate_reset_anchors)                                                     \
  X(sim_bouncer_overflow_fixture)                                              \
  X(sim_far_cell)                                                              \
  X(sim_two_r_permutations)                                                    \
  X(sim_preview_isolation)                                                     \
  X(sim_clear_and_prune)                                                       \
  X(ccout_glide_continuity)                                                    \
  X(ccout_glide_from_instant)                                                  \
  X(ccout_unknown_last_value)                                                  \
  X(ccout_instant_rates)                                                       \
  X(ccout_instant_cancels_glide)                                               \
  X(ccout_noop_submit)                                                         \
  X(ccout_noop_step_held_back)                                                 \
  X(ccout_early_end)                                                           \
  X(ccout_dedup)                                                               \
  X(ccout_glide_hz)                                                            \
  X(ccout_fixed_length)                                                        \
  X(ccout_retarget)                                                            \
  X(ccout_same_glide_again)                                                    \
  X(ccout_same_target_new_rate)                                                \
  X(ccout_same_glide_new_tempo)                                                \
  X(ccout_cancel_keeps_last_value)                                             \
  X(ccout_forget)                                                              \
  X(ccout_next_deadline)                                                       \
  X(ccout_time_goes_back)                                                      \
  X(ccout_worst_case_magnitude)                                                \
  X(ccout_moving_target)                                                       \
  X(ccout_rule_unknown_before_same)                                            \
  X(ccout_rule_same_before_noop)                                               \
  X(ccout_tick_len_guards)                                                     \
  X(ccout_poll_before_start)                                                   \
  X(ccout_poll_slot_order)                                                     \
  X(ccout_init_and_configure)                                                  \
  X(tick_cap9_resume_after_edit)                                               \
  X(tick_cap9_resume_without_edit)                                             \
  X(tick_wire_order)                                                           \
  X(tick_cci_same_tick)                                                        \
  X(tick_engine_poll_order)                                                    \
  X(tick_engine_cc_path)                                                       \
  X(tick_release_all)                                                          \
  X(tick_note_length)                                                          \
  X(tick_note_length_beat_clock)                                               \
  X(tick_note_length_0_and_1)                                                  \
  X(tick_note_release_order)                                                   \
  X(tick_note_retrigger)                                                       \
  X(tick_note_mono)                                                            \
  X(tick_note_empty_list)                                                      \
  X(tick_run_vm_clears_list)                                                   \
  X(tick_len_us)                                                               \
  X(music_scale_selectors)                                                     \
  X(music_midichord_selectors)                                                 \
  X(music_scale_inversion)                                                     \
  X(music_midichord_inversion)                                                 \
  X(music_inversion_sorts_12)                                                  \
  X(music_other_glyphs)                                                        \
  X(music_octave_overflow)                                                     \
  X(music_names)                                                               \
  X(music_shared_intervals)                                                    \
  X(music_velocity_midichord)                                                  \
  X(music_velocity_scale_points)                                               \
  X(music_velocity_midi_unchanged)                                             \
  X(music_velocity_zero)

// The full list for each adapter FEAT_ flag, and for the runner's failure
// canary, whatever the flags.
#define PORTMIDI_TESTS_ALL(X) X(portmidi_error_text_and_filters)
#define ALSA_TESTS_ALL(X) X(alsa_version_and_open_modes)
#define CANARY_TESTS_ALL(X) X(unit_check_canary)

// Every test's prototype, generated from the lists and outside any #ifdef:
// a test file whose flag is off still holds these declarations, so it is
// never an empty translation unit (which -Wpedantic rejects).
#define UNIT_TEST_DECLARE(name) void test_##name(void);
CORE_TESTS(UNIT_TEST_DECLARE)
PORTMIDI_TESTS_ALL(UNIT_TEST_DECLARE)
ALSA_TESTS_ALL(UNIT_TEST_DECLARE)
CANARY_TESTS_ALL(UNIT_TEST_DECLARE)
#undef UNIT_TEST_DECLARE

// The lists the runner runs. An adapter list is empty unless its flag is set,
// and the runner fails a build that sets the flag but registers nothing in
// its list, so adapter tests cannot be compiled out silently.
#ifdef FEAT_PORTMIDI
#define PORTMIDI_TESTS(X) PORTMIDI_TESTS_ALL(X)
#else
#define PORTMIDI_TESTS(X)
#endif
#ifdef FEAT_ALSA
#define ALSA_TESTS(X) ALSA_TESTS_ALL(X)
#else
#define ALSA_TESTS(X)
#endif

// -DUNIT_TESTS_CANARY adds a test whose check is false; CI uses it to prove
// that the runner reports a failure and exits 1.
#ifdef UNIT_TESTS_CANARY
#define CANARY_TESTS(X) CANARY_TESTS_ALL(X)
#else
#define CANARY_TESTS(X)
#endif

// A test_* function that no list declares has no prototype, so defining it
// is an error rather than a test that never runs. Helpers in test files must
// be static.
#pragma GCC diagnostic error "-Wmissing-prototypes"
