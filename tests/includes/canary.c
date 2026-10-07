// The canary for tests/check-includes.sh (architecture spine AD-2). The check
// scans this file first, the same way as a CORE file, and must report
// <stdio.h> for it on every run; if it does not, the check is blind and fails.
// No build target compiles this file.
#include <stdio.h>
