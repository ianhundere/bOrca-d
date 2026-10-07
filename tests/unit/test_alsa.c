// Adapter tests for ALSA. They build only with --alsa (FEAT_ALSA) and may
// include its header. Without the flag this file holds only the declarations
// from tests.h. They need no sequencer device.
#include "tests.h"

#ifdef FEAT_ALSA
#include <alsa/asoundlib.h>

// Proves that the FEAT_ALSA test build compiles, links and runs libasound
// code, and pins the open-mode facts I6's duplex client relies on.
void test_alsa_version_and_open_modes(void) {
  char const *version = snd_asoundlib_version();
  CHECK(version != NULL);
  CHECK(version != NULL && version[0] != '\0');
  // I6 reopens the client duplex; duplex must be exactly output plus input.
  int duplex = SND_SEQ_OPEN_DUPLEX;
  int output = SND_SEQ_OPEN_OUTPUT;
  int input = SND_SEQ_OPEN_INPUT;
  CHECK(duplex == (output | input));
  // The client is opened nonblocking, so the mode flag must be set.
  int nonblock = SND_SEQ_NONBLOCK;
  CHECK(nonblock != 0);
}
#endif
