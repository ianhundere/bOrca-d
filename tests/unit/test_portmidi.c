// Adapter tests for PortMidi. They build only with --portmidi
// (FEAT_PORTMIDI) and may include its header. Without the flag this file
// holds only the declarations from tests.h.
#include "tests.h"

#ifdef FEAT_PORTMIDI
#include <portmidi.h>
#include <stddef.h>

// Proves that the FEAT_PORTMIDI test build compiles, links and runs PortMidi
// code; I6 adds the filter-behaviour test.
void test_portmidi_error_text_and_filters(void) {
  // pmNoError returns "" in PortMidi 217, so probe an error code instead.
  char const *text = Pm_GetErrorText(pmInvalidDeviceId);
  CHECK(text != NULL);
  CHECK(text != NULL && text[0] != '\0');
  // The filter bits I6's clock input needs are set and do not overlap.
  int filt_clock = PM_FILT_CLOCK;
  int filt_play = PM_FILT_PLAY;
  int filt_song_position = PM_FILT_SONG_POSITION;
  CHECK(filt_clock != 0);
  CHECK(filt_play != 0);
  CHECK(filt_song_position != 0);
  CHECK((filt_clock & filt_play) == 0);
  CHECK((filt_clock & filt_song_position) == 0);
  CHECK((filt_play & filt_song_position) == 0);
}
#endif
