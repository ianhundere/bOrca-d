// The canary for tests/check-includes.sh (architecture spine AD-2). The check
// scans this file first, the same way as a CORE file, and must report both
// <stdio.h> and rand for it on every run; if it misses either, the check is
// blind and fails. No build target compiles this file.
#include <stdio.h>
#include <stdlib.h>

int canary_rand(void);
int canary_rand(void) { return rand(); }
