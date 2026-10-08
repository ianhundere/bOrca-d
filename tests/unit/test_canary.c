// The runner's failure-path canaries.
//
// -DUNIT_TESTS_CANARY (CI's unsigned-char job) registers
// test_unit_check_canary, whose check is false: the runner must print
// "FAIL unit_check_canary: ..." and exit 1.
//
// -DUNIT_TESTS_UNREGISTERED_CANARY (CI's build job) defines
// test_unregistered_canary with its own prototype and in no list, so it
// builds but never runs: tests/check-debug-build.sh must name it.
//
// Without either flag this file holds only the declarations from tests.h.
#include "tests.h"

#ifdef UNIT_TESTS_CANARY
void test_unit_check_canary(void) {
  int canary = 0;
  CHECK(canary == 1);
}
#endif

#ifdef UNIT_TESTS_UNREGISTERED_CANARY
void test_unregistered_canary(void);
void test_unregistered_canary(void) {}
#endif
