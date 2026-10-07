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
  X(orca_run_smoke)

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
