#include "events_print.h"

void events_print(FILE *stream, Usz tick, Oevent const *events, Usz count) {
  for (Usz i = 0; i < count; ++i) {
    Oevent const *e = events + i;
    // No default case: a new event type must add its own (guarded) case here,
    // and -Wswitch catches the omission.
    switch ((Oevent_types)e->any.oevent_type) {
    case Oevent_type_midi_note: {
      Oevent_midi_note const *em = &e->midi_note;
      // The note number as the TUI sends it: octave * 12 + note, capped at
      // 127. The cap is defensive: the VM keeps octave at 9 or below, so 119
      // is the highest value today. Channel is printed as the VM emitted it.
      Usz note_number = (Usz)(12u * em->octave + em->note);
      if (note_number > 127)
        note_number = 127;
      fprintf(stream, "t%zu NOTE ch%u note%zu vel%u len%u mono%u\n", tick,
              (unsigned)em->channel, note_number, (unsigned)em->velocity,
              (unsigned)em->duration, (unsigned)em->mono);
      break;
    }
    case Oevent_type_midi_cc: {
      Oevent_midi_cc const *ec = &e->midi_cc;
      fprintf(stream, "t%zu CC ch%u cc%u val%u\n", tick, (unsigned)ec->channel,
              (unsigned)ec->control, (unsigned)ec->value);
      break;
    }
#ifdef ORCA_VMIO_BORCA
    case Oevent_type_midi_cc_interpolated: {
      Oevent_midi_cc_interpolated const *eci = &e->midi_cc_interpolated;
      fprintf(stream, "t%zu CCI ch%u cc%u target%u rate%u\n", tick,
              (unsigned)eci->channel, (unsigned)eci->control,
              (unsigned)eci->target_value, (unsigned)eci->interpolation_rate);
      break;
    }
#endif
    case Oevent_type_midi_pb: {
      Oevent_midi_pb const *ep = &e->midi_pb;
      fprintf(stream, "t%zu PB ch%u msb%u lsb%u\n", tick, (unsigned)ep->channel,
              (unsigned)ep->msb, (unsigned)ep->lsb);
      break;
    }
    case Oevent_type_osc_ints: {
      Oevent_osc_ints const *eo = &e->osc_ints;
      fprintf(stream, "t%zu OSC %c", tick, eo->glyph);
      for (Usz j = 0; j < eo->count; ++j)
        fprintf(stream, " %u", (unsigned)eo->numbers[j]);
      fputc('\n', stream);
      break;
    }
    case Oevent_type_udp_string: {
      Oevent_udp_string const *eu = &e->udp_string;
      fprintf(stream, "t%zu UDP ", tick);
      fwrite(eu->chars, 1, (size_t)eu->count, stream);
      fputc('\n', stream);
      break;
    }
    }
  }
}
