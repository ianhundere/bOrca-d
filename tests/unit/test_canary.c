// The runner's failure-path canary. It builds only with -DUNIT_TESTS_CANARY
// (CI's unsigned-char job), where the runner must print
// "FAIL unit_check_canary: ..." and exit 1. Without the flag this file holds
// only the declarations from tests.h.
#include "tests.h"

#ifdef UNIT_TESTS_CANARY
void test_unit_check_canary(void) {
  int canary = 0;
  CHECK(canary == 1);
}
#endif
