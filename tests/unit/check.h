// CHECK for the unit tests in build/unit_tests. See tests/README.md.
#pragma once

// Called by CHECK when its expression is false: prints
// "FAIL <test>: <file>:<line>: <expr>" and marks the running test failed.
// Defined in main.c.
void unit_check_failed(char const *file, int line, char const *expr);

// Records a failure and lets the test continue, so one run reports every
// failed check. Never use assert() in a test: release builds define NDEBUG,
// which removes it.
#define CHECK(expr)                                                            \
  ((expr) ? (void)0 : unit_check_failed(__FILE__, __LINE__, #expr))
