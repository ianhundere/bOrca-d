// build/unit_tests: runs every registered unit test. See tests/README.md.
//
// Prints "ok <name>" for each test that passes and one
// "FAIL <name>: <file>:<line>: <expr>" line for each failed CHECK, then
// "<n> tests, <m> failed". Exits 0 when no test failed, 1 otherwise.
#include "tests.h"
#include <stddef.h>
#include <stdio.h>

typedef struct {
  char const *name;
  void (*run)(void);
} Unit_test;

static Unit_test const unit_tests[] = {
#define X(name) {#name, test_##name},
    CORE_TESTS(X) PORTMIDI_TESTS(X) ALSA_TESTS(X) CANARY_TESTS(X)
#undef X
};

#define COUNT(name) +1
enum { portmidi_test_count = 0 PORTMIDI_TESTS(COUNT) };
enum { alsa_test_count = 0 ALSA_TESTS(COUNT) };
#undef COUNT

static char const *current_test = "";
static size_t current_failures;

// UBSan's default options in sanitized builds: stop at the first report, so
// it fails the run even when UBSAN_OPTIONS is not set. UBSAN_OPTIONS still
// overrides them. Unused in builds without UBSan.
char const *__ubsan_default_options(void);
char const *__ubsan_default_options(void) {
  return "halt_on_error=1:print_stacktrace=1";
}

void unit_check_failed(char const *file, int line, char const *expr) {
  printf("FAIL %s: %s:%d: %s\n", current_test, file, line, expr);
  ++current_failures;
}

int main(void) {
  // A FEAT_ build must register at least one test for its flag.
  char const *empty_flag = NULL;
#ifdef FEAT_PORTMIDI
  if (portmidi_test_count == 0)
    empty_flag = "FEAT_PORTMIDI";
#endif
#ifdef FEAT_ALSA
  if (alsa_test_count == 0)
    empty_flag = "FEAT_ALSA";
#endif
  if (empty_flag != NULL) {
    fprintf(stderr,
            "unit_tests: %s is set but registers no adapter tests; add one "
            "to its list in tests/unit/tests.h\n",
            empty_flag);
    return 1;
  }
  size_t count = sizeof unit_tests / sizeof unit_tests[0];
  size_t failed = 0;
  for (size_t i = 0; i < count; ++i) {
    current_test = unit_tests[i].name;
    current_failures = 0;
    unit_tests[i].run();
    if (current_failures == 0)
      printf("ok %s\n", current_test);
    else
      ++failed;
    // Keep the result lines in order with any sanitizer report on stderr.
    fflush(stdout);
  }
  printf("%zu tests, %zu failed\n", count, failed);
  if (fflush(stdout) != 0 || ferror(stdout)) {
    perror("unit_tests: stdout");
    return 1;
  }
  return failed == 0 ? 0 : 1;
}
