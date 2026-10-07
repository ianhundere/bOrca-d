// The canary for tests/check-nm.sh (architecture spine AD-1). The check must
// flag each of the four symbols below by name on every run; if it misses one,
// the check is blind to that symbol class and fails.
//
// One writable symbol per class: a zero-initialised static (b), an
// initialised static (d), a zero-initialised external (B; the explicit
// initialiser keeps it out of common under -fcommon) and an initialised
// external (D). canary_address returns each one's address, so -O2 keeps
// them all: an unused or read-only static is dropped, and the check would then
// fail the object only for being empty. No build target links this file.
extern int canary_extern_zero;
extern int canary_extern_init;
int *canary_address(int which);

static int canary_static_zero = 0;
static int canary_static_init = 1;
int canary_extern_zero = 0;
int canary_extern_init = 1;

int *canary_address(int which) {
  switch (which) {
  case 0:
    return &canary_static_zero;
  case 1:
    return &canary_static_init;
  case 2:
    return &canary_extern_zero;
  default:
    return &canary_extern_init;
  }
}
