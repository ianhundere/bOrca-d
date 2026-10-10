#include "sim.h"
#include "gbuffer.h"
#include "music.h"
#include <string.h>

// Note Sequence
static char const note_sequence[] = "CcDdEFfGgAaB";

Usz find_note_index(Glyph root_note_glyph) {
  for (Usz i = 0; i < sizeof(note_sequence) - 1; i++) {
    if (note_sequence[i] == root_note_glyph)
      return i;
  }
  return UINT_MAX; // Indicate not found
}

//////// Utilities

static Glyph const glyph_table[36] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', //  0-11
    'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', // 12-23
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', // 24-35
};
enum { Glyphs_index_count = sizeof glyph_table };
static inline Glyph glyph_of(Usz index) {
  assert(index < Glyphs_index_count);
  return glyph_table[index];
}

static U8 const index_table[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //   0-15
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //  16-31
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //  32-47
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  0,  0,  0,  0,  0,  //  48-63
    0,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //  64-79
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0,  0,  0,  0,  0,  //  80-95
    0,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //  96-111
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0,  0,  0,  0,  0}; // 112-127
static ORCA_FORCEINLINE Usz index_of(Glyph c) { return index_table[c & 0x7f]; }

// Reference implementation:
// static Usz index_of(Glyph c) {
//   if (c >= '0' && c <= '9') return (Usz)(c - '0');
//   if (c >= 'A' && c <= 'Z') return (Usz)(c - 'A' + 10);
//   if (c >= 'a' && c <= 'z') return (Usz)(c - 'a' + 10);
//   return 0;
// }

static ORCA_FORCEINLINE bool glyph_is_lowercase(Glyph g) { return g & 1 << 5; }
static ORCA_FORCEINLINE Glyph glyph_lowered_unsafe(Glyph g) {
  return (Glyph)(g | 1 << 5);
}
static inline Glyph glyph_with_case(Glyph g, Glyph caser) {
  enum { Case_bit = 1 << 5, Alpha_bit = 1 << 6 };
  return (Glyph)((g & ~Case_bit) | ((~g & Alpha_bit) >> 1) |
                 (caser & Case_bit));
}

static ORCA_PURE bool oper_has_neighboring_bang(Glyph const *gbuf, Usz h, Usz w,
                                                Usz y, Usz x) {
  Glyph const *gp = gbuf + w * y + x;
  if (x < w - 1 && gp[1] == '*')
    return true;
  if (x > 0 && *(gp - 1) == '*')
    return true;
  if (y < h - 1 && gp[w] == '*')
    return true;
  // note: negative array subscript on rhs of short-circuit, may cause ub if
  // the arithmetic under/overflows, even if guarded the guard on lhs is false
  if (y > 0 && *(gp - w) == '*')
    return true;
  return false;
}

// Returns UINT8_MAX if not a valid note.
static U8 midi_note_number_of(Glyph g) {
  int sharp = (g & 1 << 5) >> 5; // sharp=1 if lowercase
  g &= (Glyph) ~(1 << 5);        // make uppercase
  if (g < 'A' || g > 'Z')        // A through Z only
    return UINT8_MAX;
  // We want C=0, D=1, E=2, etc. A and B are equivalent to H and I.
  int deg = g <= 'B' ? 'G' - 'B' + g - 'A' : g - 'C';
  return (U8)(deg / 7 * 12 + (I8[]){0, 2, 4, 5, 7, 9, 11}[deg % 7] + sharp);
}

// The velocity of `:`, `%` and `=` (CAP-10). `.` is 127. The value comes
// from index_of, which folds case (`F` is `f`) and ignores the top bit, so a
// byte 0x80 or above reads like its low seven bits. A glyph worth 0, `0` or
// any other glyph that is not 0-9, a-z or A-Z, returns 0: send no note. Any
// other value v gives min(v * 8 - 1, 127), so `1` is 7, `f` 119 and `g` to
// `z` 127. The test is on the value, not the glyph `'0'`: 0 * 8 - 1 would
// wrap and clamp to 127.
static U8 midi_velocity_of(Glyph g) {
  if (g == '.')
    return 127;
  Usz v = index_of(g);
  if (v == 0)
    return 0;
  v = v * 8 - 1;
  return (U8)(v > 127 ? 127 : v);
}

typedef struct {
  Glyph *vars_slots;
  Oevent_list *oevent_list;
  Usz random_seed;
  Orca_run_ctx const *ctx;
} Oper_extra_params;

static void oper_poke_and_stun(Glyph *restrict gbuffer, Mark *restrict mbuffer,
                               Usz height, Usz width, Usz y, Usz x, Isz delta_y,
                               Isz delta_x, Glyph g) {
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 < 0 || x0 < 0 || (Usz)y0 >= height || (Usz)x0 >= width)
    return;
  Usz offs = (Usz)y0 * width + (Usz)x0;
  gbuffer[offs] = g;
  mbuffer[offs] |= Mark_flag_sleep;
}

// For anyone editing this in the future: the "no inline" here is deliberate.
// You may think that inlining is always faster. Or even just letting the
// compiler decide. You would be wrong. Try it. If you really want this VM to
// run faster, you will need to use computed goto or assembly.
#define OPER_FUNCTION_ATTRIBS ORCA_NOINLINE static void

#define BEGIN_OPERATOR(_oper_name)                                             \
  OPER_FUNCTION_ATTRIBS oper_behavior_##_oper_name(                            \
      Glyph *const restrict gbuffer, Mark *const restrict mbuffer,             \
      Usz const height, Usz const width, Usz const y, Usz const x,             \
      Usz Tick_number, Oper_extra_params *const extra_params,                  \
      Mark const cell_flags, Glyph const This_oper_char) {                     \
    (void)gbuffer;                                                             \
    (void)mbuffer;                                                             \
    (void)height;                                                              \
    (void)width;                                                               \
    (void)y;                                                                   \
    (void)x;                                                                   \
    (void)Tick_number;                                                         \
    (void)extra_params;                                                        \
    (void)cell_flags;                                                          \
    (void)This_oper_char;

#define END_OPERATOR }

#define PEEK(_delta_y, _delta_x)                                               \
  gbuffer_peek_relative(gbuffer, height, width, y, x, _delta_y, _delta_x)
#define POKE(_delta_y, _delta_x, _glyph)                                       \
  gbuffer_poke_relative(gbuffer, height, width, y, x, _delta_y, _delta_x,      \
                        _glyph)
#define STUN(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_sleep)
#define POKE_STUNNED(_delta_y, _delta_x, _glyph)                               \
  oper_poke_and_stun(gbuffer, mbuffer, height, width, y, x, _delta_y,          \
                     _delta_x, _glyph)
#define LOCK(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_lock)

#define IN Mark_flag_input
#define OUT Mark_flag_output
#define NONLOCKING Mark_flag_lock
#define PARAM Mark_flag_haste_input

#define LOWERCASE_REQUIRES_BANG                                                \
  if (glyph_is_lowercase(This_oper_char) &&                                    \
      !oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define STOP_IF_NOT_BANGED                                                     \
  if (!oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define PORT(_delta_y, _delta_x, _flags, _tooltip)                             \
  do {                                                                         \
    mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,     \
                                   _delta_x, (_flags) ^ Mark_flag_lock);        \
    (void)(_tooltip); /* Suppress unused parameter warning for now */          \
  } while(0)
//////// Operators

#define UNIQUE_OPERATORS(_)                                                    \
  _('!', midicc)                                                               \
  _('#', comment)                                                              \
  _('$', scale)                                                                \
  _('%', midi)                                                                 \
  _('*', bang)                                                                 \
  _(':', midi)                                                                 \
  _(';', arpeggiator)                                                          \
  _('=', midichord)                                                            \
  _('?', midipb)                                                               \
  _('&', bouncer)

#define ALPHA_OPERATORS(_)                                                     \
  _('A', add)                                                                  \
  _('B', subtract)                                                             \
  _('C', clock)                                                                \
  _('D', delay)                                                                \
  _('E', movement)                                                             \
  _('F', if)                                                                   \
  _('G', generator)                                                            \
  _('H', halt)                                                                 \
  _('I', increment)                                                            \
  _('J', jump)                                                                 \
  _('K', konkat)                                                               \
  _('L', lesser)                                                               \
  _('M', multiply)                                                             \
  _('N', movement)                                                             \
  _('O', offset)                                                               \
  _('P', push)                                                                 \
  _('Q', query)                                                                \
  _('R', random)                                                               \
  _('S', movement)                                                             \
  _('T', track)                                                                \
  _('U', uclid)                                                                \
  _('V', variable)                                                             \
  _('W', movement)                                                             \
  _('X', teleport)                                                             \
  _('Y', yump)                                                                 \
  _('Z', lerp)

BEGIN_OPERATOR(movement)
  if (glyph_is_lowercase(This_oper_char) &&
      !oper_has_neighboring_bang(gbuffer, height, width, y, x))
    return;
  Isz delta_y, delta_x;
  switch (glyph_lowered_unsafe(This_oper_char)) {
  case 'n':
    delta_y = -1;
    delta_x = 0;
    break;
  case 'e':
    delta_y = 0;
    delta_x = 1;
    break;
  case 's':
    delta_y = 1;
    delta_x = 0;
    break;
  case 'w':
    delta_y = 0;
    delta_x = -1;
    break;
  default:
    // could cause strict aliasing problem, maybe
    delta_y = 0;
    delta_x = 0;
    break;
  }
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0) {
    gbuffer[y * width + x] = '*';
    return;
  }
  Glyph *restrict g_at_dest = gbuffer + (Usz)y0 * width + (Usz)x0;
  if (*g_at_dest == '.') {
    *g_at_dest = This_oper_char;
    gbuffer[y * width + x] = '.';
    mbuffer[(Usz)y0 * width + (Usz)x0] |= Mark_flag_sleep;
  } else {
    gbuffer[y * width + x] = '*';
  }
END_OPERATOR

BEGIN_OPERATOR(midicc)
  PORT(0, 1, IN | PARAM, "Channel");
  PORT(0, 2, IN | PARAM, "Control (hundreds)");
  PORT(0, 3, IN | PARAM, "Control (tens)");
  PORT(0, 4, IN | PARAM, "Control (ones)");
  PORT(0, 5, IN | PARAM, "Value");
  PORT(0, 6, IN | PARAM, "Interpolation Rate");
  STOP_IF_NOT_BANGED;
  Glyph channel_g = PEEK(0, 1);
  Glyph control_h = PEEK(0, 2);  // hundreds
  Glyph control_t = PEEK(0, 3);  // tens
  Glyph control_o = PEEK(0, 4);  // ones
  Glyph value_g = PEEK(0, 5);
  Glyph interp_rate_g = PEEK(0, 6);

  if (channel_g == '.' || value_g == '.')
    return;
  
  Usz channel = index_of(channel_g);
  if (channel > 15)
    return;
  
  // Calculate CC number from hundreds, tens, ones (support . for missing digits)
  Usz control_num = 0;
  if (control_h != '.') control_num += index_of(control_h) * 100;
  if (control_t != '.') control_num += index_of(control_t) * 10;
  if (control_o != '.') control_num += index_of(control_o);
  
  // Clamp to valid MIDI CC range (0-127)
  if (control_num > 127) control_num = 127;
  
  // Map ORCA values (0-35) to MIDI CC values in increments of 4: 0→0, 1→4, 2→8, etc.
  Usz value_index = index_of(value_g);
  Usz midi_value = value_index * 4;
  if (midi_value > 127) midi_value = 127; // Clamp to MIDI CC range
  
  PORT(0, 0, OUT, "");
  
  // Handle interpolation rate
  if (interp_rate_g == '.') {
    // No interpolation - send immediate CC
    Oevent_midi_cc *oe =
        (Oevent_midi_cc *)oevent_list_alloc_item(extra_params->oevent_list);
    oe->oevent_type = Oevent_type_midi_cc;
    oe->channel = (U8)channel;
    oe->control = (U8)control_num;
    oe->value = (U8)midi_value;
  } else {
    // Send interpolated CC event
    Oevent_midi_cc_interpolated *oe =
        (Oevent_midi_cc_interpolated *)oevent_list_alloc_item(extra_params->oevent_list);
    oe->oevent_type = Oevent_type_midi_cc_interpolated;
    oe->channel = (U8)channel;
    oe->control = (U8)control_num;
    oe->target_value = (U8)midi_value;
    oe->interpolation_rate = (U8)index_of(interp_rate_g);
  }
END_OPERATOR

BEGIN_OPERATOR(comment)
  // restrict probably ok here...
  Glyph const *restrict gline = gbuffer + y * width;
  Mark *restrict mline = mbuffer + y * width;
  Usz max_x = x + 255;
  if (width < max_x)
    max_x = width;
  for (Usz x0 = x + 1; x0 < max_x; ++x0) {
    Glyph g = gline[x0];
    mline[x0] |= (Mark)Mark_flag_lock;
    if (g == '#')
      break;
  }
END_OPERATOR

BEGIN_OPERATOR(bang)
  gbuffer_poke(gbuffer, height, width, y, x, '.');
END_OPERATOR

BEGIN_OPERATOR(midi)
  PORT(0, 1, IN | PARAM, "Channel");
  PORT(0, 2, IN | PARAM, "Octave");
  PORT(0, 3, IN | PARAM, "Note");
  PORT(0, 4, IN | PARAM, "Velocity");
  PORT(0, 5, IN | PARAM, "Length");
  STOP_IF_NOT_BANGED;
  Glyph channel_g = PEEK(0, 1);
  Glyph octave_g = PEEK(0, 2);
  Glyph note_g = PEEK(0, 3);
  Glyph velocity_g = PEEK(0, 4);
  Glyph length_g = PEEK(0, 5);
  U8 octave_num = (U8)index_of(octave_g);
  if (octave_g == '.')
    return;
  if (octave_num > 9)
    octave_num = 9;
  U8 note_num = midi_note_number_of(note_g);
  if (note_num == UINT8_MAX)
    return;
  Usz channel_num = index_of(channel_g);
  if (channel_num > 15)
    channel_num = 15;
  U8 velocity = midi_velocity_of(velocity_g);
  if (velocity == 0)
    return;
  PORT(0, 0, OUT, "");
  Oevent_midi_note *oe =
      (Oevent_midi_note *)oevent_list_alloc_item(extra_params->oevent_list);
  oe->oevent_type = (U8)Oevent_type_midi_note;
  oe->channel = (U8)channel_num;
  oe->octave = octave_num;
  oe->note = note_num;
  oe->velocity = velocity;
  oe->duration = (U8)(index_of(length_g) & 0x7Fu);
  oe->mono = This_oper_char == '%' ? 1 : 0;
END_OPERATOR

BEGIN_OPERATOR(midipb)
  PORT(0, 1, IN | PARAM, "Channel");
  PORT(0, 2, IN | PARAM, "MSB");
  PORT(0, 3, IN | PARAM, "LSB");
  PORT(0, 0, OUT, ""); // Mark output immediately
  STOP_IF_NOT_BANGED;
  Glyph channel_g = PEEK(0, 1);
  Glyph msb_g = PEEK(0, 2);
  Glyph lsb_g = PEEK(0, 3);
  if (channel_g == '.')
    return;
  Usz channel = index_of(channel_g);
  if (channel > 15)
    return;
  Oevent_midi_pb *oe =
      (Oevent_midi_pb *)oevent_list_alloc_item(extra_params->oevent_list);
  oe->oevent_type = Oevent_type_midi_pb;
  oe->channel = (U8)channel;
  oe->msb = (U8)(index_of(msb_g) * 127 / 35); // 0~35 -> 0~127
  oe->lsb = (U8)(index_of(lsb_g) * 127 / 35);
END_OPERATOR

BEGIN_OPERATOR(add)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Value A");
  PORT(0, 1, IN, "Value B");
  PORT(1, 0, OUT, "");
  Glyph a = PEEK(0, -1);
  Glyph b = PEEK(0, 1);
  Glyph g = glyph_table[(index_of(a) + index_of(b)) % Glyphs_index_count];
  POKE(1, 0, glyph_with_case(g, b));
END_OPERATOR

BEGIN_OPERATOR(subtract)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Value A");
  PORT(0, 1, IN, "Value B");
  PORT(1, 0, OUT, "");
  Glyph a = PEEK(0, -1);
  Glyph b = PEEK(0, 1);
  Isz val = (Isz)index_of(b) - (Isz)index_of(a);
  if (val < 0)
    val = -val;
  POKE(1, 0, glyph_with_case(glyph_of((Usz)val), b));
END_OPERATOR

BEGIN_OPERATOR(clock)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Rate");
  PORT(0, 1, IN, "Modulo");
  PORT(1, 0, OUT, "");
  Glyph b = PEEK(0, 1);
  Usz rate = index_of(PEEK(0, -1));
  Usz mod_num = index_of(b);
  if (rate == 0)
    rate = 1;
  if (mod_num == 0)
    mod_num = 8;
  Glyph g = glyph_of(Tick_number / rate % mod_num);
  POKE(1, 0, glyph_with_case(g, b));
END_OPERATOR

BEGIN_OPERATOR(delay)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Rate");
  PORT(0, 1, IN, "Modulo");
  PORT(1, 0, OUT, "");
  Usz rate = index_of(PEEK(0, -1));
  Usz mod_num = index_of(PEEK(0, 1));
  if (rate == 0)
    rate = 1;
  if (mod_num == 0)
    mod_num = 8;
  Glyph g = Tick_number % (rate * mod_num) == 0 ? '*' : '.';
  POKE(1, 0, g);
END_OPERATOR

BEGIN_OPERATOR(if)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Value A");
  PORT(0, 1, IN, "Value B");
  PORT(1, 0, OUT, "");
  Glyph g0 = PEEK(0, -1);
  Glyph g1 = PEEK(0, 1);
  POKE(1, 0, g0 == g1 ? '*' : '.');
END_OPERATOR

BEGIN_OPERATOR(generator)
  LOWERCASE_REQUIRES_BANG;
  Isz out_x = (Isz)index_of(PEEK(0, -3));
  Isz out_y = (Isz)index_of(PEEK(0, -2)) + 1;
  Isz len = (Isz)index_of(PEEK(0, -1));
  PORT(0, -3, IN | PARAM, "X offset"); // x
  PORT(0, -2, IN | PARAM, "Y offset"); // y
  PORT(0, -1, IN | PARAM, "Length"); // len
  for (Isz i = 0; i < len; ++i) {
    PORT(0, i + 1, IN | PARAM, "Input");
    PORT(out_y, out_x + i, OUT | NONLOCKING, "");
    Glyph g = PEEK(0, i + 1);
    POKE_STUNNED(out_y, out_x + i, g);
  }
END_OPERATOR

BEGIN_OPERATOR(halt)
  LOWERCASE_REQUIRES_BANG;
  PORT(1, 0, IN | PARAM, "Input");
END_OPERATOR

BEGIN_OPERATOR(increment)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Rate");
  PORT(0, 1, IN, "Max");
  PORT(1, 0, IN | OUT, "");
  Glyph ga = PEEK(0, -1);
  Glyph gb = PEEK(0, 1);
  Usz rate = 1;
  if (ga != '.' && ga != '*')
    rate = index_of(ga);
  Usz max = index_of(gb);
  Usz val = index_of(PEEK(1, 0));
  if (max == 0)
    max = 36;
  val = val + rate;
  val = val % max;
  POKE(1, 0, glyph_with_case(glyph_of(val), gb));
END_OPERATOR

BEGIN_OPERATOR(jump)
  LOWERCASE_REQUIRES_BANG;
  Glyph g = PEEK(-1, 0);
  if (g == 'J')
    return;
  PORT(-1, 0, IN, "Input");
  for (Isz i = 1; i <= 256; ++i) {
    if (PEEK(i, 0) != This_oper_char) {
      PORT(i, 0, OUT, "");
      POKE(i, 0, g);
      break;
    }
    STUN(i, 0);
  }
END_OPERATOR

// Note: this is merged from a pull request without being fully tested or
// optimized
BEGIN_OPERATOR(konkat)
  LOWERCASE_REQUIRES_BANG;
  Isz len = (Isz)index_of(PEEK(0, -1));
  if (len == 0)
    len = 1;
  PORT(0, -1, IN | PARAM, "Length");
  for (Isz i = 0; i < len; ++i) {
    PORT(0, i + 1, IN | PARAM, "Variable");
    Glyph var = PEEK(0, i + 1);
    if (var != '.') {
      Usz var_idx = index_of(var);
      Glyph result = extra_params->vars_slots[var_idx];
      PORT(1, i + 1, OUT, "");
      POKE(1, i + 1, result);
    }
  }
END_OPERATOR

BEGIN_OPERATOR(lesser)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Value A");
  PORT(0, 1, IN, "Value B");
  PORT(1, 0, OUT, "");
  Glyph ga = PEEK(0, -1);
  Glyph gb = PEEK(0, 1);
  if (ga == '.' || gb == '.') {
    POKE(1, 0, '.');
  } else {
    Usz ia = index_of(ga);
    Usz ib = index_of(gb);
    Usz out = ia < ib ? ia : ib;
    POKE(1, 0, glyph_with_case(glyph_of(out), gb));
  }
END_OPERATOR

BEGIN_OPERATOR(multiply)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Factor A");
  PORT(0, 1, IN, "Factor B");
  PORT(1, 0, OUT, "");
  Glyph a = PEEK(0, -1);
  Glyph b = PEEK(0, 1);
  Glyph g = glyph_table[(index_of(a) * index_of(b)) % Glyphs_index_count];
  POKE(1, 0, glyph_with_case(g, b));
END_OPERATOR

BEGIN_OPERATOR(offset)
  LOWERCASE_REQUIRES_BANG;
  Isz in_x = (Isz)index_of(PEEK(0, -2)) + 1;
  Isz in_y = (Isz)index_of(PEEK(0, -1));
  PORT(0, -1, IN | PARAM, "Y offset");
  PORT(0, -2, IN | PARAM, "X offset");
  PORT(in_y, in_x, IN, "Input");
  PORT(1, 0, OUT, "");
  POKE(1, 0, PEEK(in_y, in_x));
END_OPERATOR

BEGIN_OPERATOR(push)
  LOWERCASE_REQUIRES_BANG;
  Usz key = index_of(PEEK(0, -2));
  Usz len = index_of(PEEK(0, -1));
  PORT(0, -1, IN | PARAM, "Length");
  PORT(0, -2, IN | PARAM, "Key");
  PORT(0, 1, IN, "Input");
  if (len == 0)
    return;
  Isz out_x = (Isz)(key % len);
  for (Usz i = 0; i < len; ++i) {
    LOCK(1, (Isz)i);
  }
  PORT(1, out_x, OUT, "");
  POKE(1, out_x, PEEK(0, 1));
END_OPERATOR

BEGIN_OPERATOR(query)
  LOWERCASE_REQUIRES_BANG;
  Isz in_x = (Isz)index_of(PEEK(0, -3)) + 1;
  Isz in_y = (Isz)index_of(PEEK(0, -2));
  Isz len = (Isz)index_of(PEEK(0, -1));
  Isz out_x = 1 - len;
  PORT(0, -3, IN | PARAM, "X offset"); // x
  PORT(0, -2, IN | PARAM, "Y offset"); // y
  PORT(0, -1, IN | PARAM, "Length"); // len
  // todo direct buffer manip
  for (Isz i = 0; i < len; ++i) {
    PORT(in_y, in_x + i, IN, "Input");
    PORT(1, out_x + i, OUT, "");
    Glyph g = PEEK(in_y, in_x + i);
    POKE(1, out_x + i, g);
  }
END_OPERATOR

BEGIN_OPERATOR(track)
  LOWERCASE_REQUIRES_BANG;
  Usz key = index_of(PEEK(0, -2));
  Usz len = index_of(PEEK(0, -1));
  PORT(0, -2, IN | PARAM, "Key");
  PORT(0, -1, IN | PARAM, "Length");
  if (len == 0)
    return;
  Isz read_val_x = (Isz)(key % len) + 1;
  for (Usz i = 0; i < len; ++i) {
    LOCK(0, (Isz)(i + 1));
  }
  PORT(0, (Isz)read_val_x, IN, "Input");
  PORT(1, 0, OUT, "");
  POKE(1, 0, PEEK(0, read_val_x));
END_OPERATOR

// https://www.computermusicdesign.com/
// simplest-euclidean-rhythm-algorithm-explained/
BEGIN_OPERATOR(uclid)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Steps");
  PORT(0, 1, IN, "Max");
  PORT(1, 0, OUT, "");
  Glyph left = PEEK(0, -1);
  Usz steps = 1;
  if (left != '.' && left != '*')
    steps = index_of(left);
  Usz max = index_of(PEEK(0, 1));
  if (max == 0)
    max = 8;
  Usz bucket = (steps * (Tick_number + max - 1)) % max + steps;
  Glyph g = (bucket >= max) ? '*' : '.';
  POKE(1, 0, g);
END_OPERATOR

BEGIN_OPERATOR(variable)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Variable");
  PORT(0, 1, IN, "Value");
  Glyph left = PEEK(0, -1);
  Glyph right = PEEK(0, 1);
  if (left != '.') {
    // Write
    Usz var_idx = index_of(left);
    extra_params->vars_slots[var_idx] = right;
  } else if (right != '.') {
    // Read
    PORT(1, 0, OUT, "");
    Usz var_idx = index_of(right);
    Glyph result = extra_params->vars_slots[var_idx];
    POKE(1, 0, result);
  }
END_OPERATOR

BEGIN_OPERATOR(teleport)
  LOWERCASE_REQUIRES_BANG;
  Isz out_x = (Isz)index_of(PEEK(0, -2));
  Isz out_y = (Isz)index_of(PEEK(0, -1)) + 1;
  PORT(0, -2, IN | PARAM, "X offset"); // x
  PORT(0, -1, IN | PARAM, "Y offset"); // y
  PORT(0, 1, IN, "Input");
  PORT(out_y, out_x, OUT | NONLOCKING, "");
  POKE_STUNNED(out_y, out_x, PEEK(0, 1));
END_OPERATOR

BEGIN_OPERATOR(yump)
  LOWERCASE_REQUIRES_BANG;
  Glyph g = PEEK(0, -1);
  if (g == 'Y')
    return;
  PORT(0, -1, IN, "Input");
  for (Isz i = 1; i <= 256; ++i) {
    if (PEEK(0, i) != This_oper_char) {
      PORT(0, i, OUT, "");
      POKE(0, i, g);
      break;
    }
    STUN(0, i);
  }
END_OPERATOR

BEGIN_OPERATOR(lerp)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, -1, IN | PARAM, "Rate");
  PORT(0, 1, IN, "Target");
  PORT(1, 0, IN | OUT, "");
  Glyph g = PEEK(0, -1);
  Glyph b = PEEK(0, 1);
  Isz rate = g == '.' || g == '*' ? 1 : (Isz)index_of(g);
  Isz goal = (Isz)index_of(b);
  Isz val = (Isz)index_of(PEEK(1, 0));
  Isz mod = val <= goal - rate ? rate : val >= goal + rate ? -rate : goal - val;
  POKE(1, 0, glyph_with_case(glyph_of((Usz)(val + mod)), b));
END_OPERATOR

// BOORCH's Scale operator. Its scales and chords come from music.h.
BEGIN_OPERATOR(scale)
  PORT(0, 1, IN | PARAM, "Octave");   // Octave input
  PORT(0, 2, IN | PARAM, "Root");   // Root note (like C, c, D etc)
  PORT(0, 3, IN | PARAM, "Scale");   // Selector: 0-9 scales, a-z chords, A-Z first inversions
  PORT(0, 4, IN | PARAM, "Degree");   // Degree
  PORT(1, -1, OUT, ""); // Octave output
  PORT(1, 0, OUT, "");  // Note output

  // Lock inputs
  LOCK(0, 1);
  LOCK(0, 2);
  LOCK(0, 3);
  LOCK(0, 4);

  Glyph octave_g = PEEK(0, 1);
  Glyph root_note_glyph = PEEK(0, 2);
  Glyph scale_glyph = PEEK(0, 3);
  Glyph degree_glyph = PEEK(0, 4);

  // Handle empty inputs
  if (root_note_glyph == '.' || scale_glyph == '.' || degree_glyph == '.') {
    POKE(1, 0, '.');
    return;
  }

  // Get root note number (0-11)
  U8 root_note_num = midi_note_number_of(root_note_glyph);
  if (root_note_num == UINT8_MAX)
    return;

  // Get base octave if provided
  Usz base_octave = 0;
  if (octave_g != '.') {
    base_octave = index_of(octave_g);
    if (base_octave > 9)
      base_octave = 9;
  }

  // The selector's intervals, decoded from its raw glyph (music.h): 0-9
  // scales, a-z chords in root position, A-Z their first inversions, and
  // scale 0 for any other glyph.
  Music_intervals intervals;
  music_decode(Music_op_scale, scale_glyph, &intervals);

  // Degrees past the last interval carry into the next octave.
  Usz degree_index = index_of(degree_glyph);
  Usz scale_length = intervals.count;
  Usz octave_increment = degree_index / scale_length;
  degree_index = degree_index % scale_length;
  Usz scale_offset = intervals.semitones[degree_index];

  // Calculate total semitones including octave increment
  Usz total_semitones = root_note_num + scale_offset + (octave_increment * 12);

  // Calculate final note and octave
  Usz final_note = total_semitones % 12;
  Usz octave_offset = total_semitones / 12;
  Usz final_octave = base_octave + octave_offset;

  if (final_octave > 9)
    return;

  // Output note
  Glyph output_note_glyph = note_sequence[final_note];
  POKE(1, 0, output_note_glyph);

  // Output octave if input octave was provided
  if (octave_g != '.') {
    POKE(1, -1, glyph_of(final_octave));
  }
END_OPERATOR

// BOORCH's MIDIChord operator. Its chords come from music.h.
BEGIN_OPERATOR(midichord)
  // Check all required input ports
  PORT(0, 1, IN | PARAM, "Channel");
  PORT(0, 2, IN | PARAM, "Octave");
  PORT(0, 3, IN | PARAM, "Root note");
  PORT(0, 4, IN | PARAM, "Chord type");
  PORT(0, 5, IN | PARAM, "Velocity");
  PORT(0, 6, IN | PARAM, "Length");
  PORT(0, 0, OUT, ""); // Mark output immediately
  STOP_IF_NOT_BANGED;

  // Get base note information with local scope
  Usz channel = index_of(PEEK(0, 1));
  if (channel > 15)
    channel = 15;

  // Get velocity and duration with standardized handling
  Glyph velocity_g = PEEK(0, 5);
  Glyph length_g = PEEK(0, 6);

  // The velocity `:` and `%` use; a glyph worth 0 sends no chord.
  U8 velocity = midi_velocity_of(velocity_g);
  if (velocity == 0)
    return;

  // Get initial octave
  int current_octave = (int)index_of(PEEK(0, 2));
  if (current_octave > 9)
    current_octave = 9;

  // Get root note and validate
  U8 root_note = midi_note_number_of(PEEK(0, 3));
  if (root_note == UINT8_MAX)
    return;

  // The chord's intervals, decoded from the selector's raw glyph (music.h):
  // 0-9 enriched chords, a-z chords in root position, A-Z their first
  // inversions, and enriched chord 0 for any other glyph.
  Music_intervals chord;
  music_decode(Music_op_midichord, PEEK(0, 4), &chord);

  // Track highest note played so far
  int last_note_absolute = (current_octave * 12) + root_note - 1;

  // Create and output midi events for each note in the chord
  for (Usz i = 0; i < chord.count; i++) {
    // Calculate this note's absolute MIDI note number
    int note_absolute =
        (current_octave * 12) + root_note + (int)chord.semitones[i];

    // If this note would be lower than previous note, move it up an octave
    while (note_absolute <= last_note_absolute) {
      current_octave++;
      note_absolute += 12;
    }

    // Skip if we exceeded MIDI range
    if (current_octave > 9 || note_absolute > 127)
      continue;

    // Keep track of highest note
    last_note_absolute = note_absolute;

    // Calculate final octave and note numbers
    U8 final_octave = (U8)(note_absolute / 12);
    U8 final_note = (U8)(note_absolute % 12);

    // Create MIDI event
    Oevent_midi_note *oe =
        (Oevent_midi_note *)oevent_list_alloc_item(extra_params->oevent_list);
    oe->oevent_type = Oevent_type_midi_note;
    oe->channel = (U8)channel;
    oe->octave = final_octave;
    oe->note = final_note;
    oe->velocity = velocity;
    oe->duration = (U8)(index_of(length_g) & 0x7Fu);
    oe->mono = 0;
  }

END_OPERATOR

// BOORCH's Simple Arpeggiator - outputs degree numbers for Scale operator
typedef enum {
  ARP_UP = 0,        // 0: Ascending
  ARP_DOWN,          // 1: Descending  
  ARP_UP_DOWN,       // 2: Up and down (without repeating highest)
  ARP_DOWN_UP,       // 3: Down and up (without repeating lowest)
  ARP_UP_DOWN_PLUS,  // 4: Up and down with repeated end points
  ARP_DOWN_UP_PLUS,  // 5: Down and up with repeated end points
  ARP_CONVERGE,      // 6: Outside in (highest, lowest, 2nd highest, 2nd lowest...)
  ARP_DIVERGE,       // 7: Inside out (middle outward)
  ARP_PINKY_UP,      // 8: Alternate between notes and highest note
  ARP_THUMB_UP,      // 9: Alternate between lowest and other notes
  ARP_UP_DOWN_ALT,   // a: Up-down alternating pattern
  ARP_DOWN_UP_ALT,   // b: Down-up alternating pattern  
  ARP_RANDOM,        // c: Random selection
  ARP_BOUNCE,        // d: Bouncing pattern
  ARP_PATTERN_COUNT
} ArpPatternType;

// Function to get the degree based on pattern type and step
static Usz get_arp_degree(ArpPatternType pattern, Usz step, Usz range, 
                         Oper_extra_params *extra_params) {
  // Assume a 7-note scale and multiply by range for total degrees
  Usz scale_length = 7;
  Usz total_degrees = scale_length * range;
  
  if (total_degrees == 0)
    return 0;

  switch (pattern) {
  case ARP_UP:
    return step % total_degrees;

  case ARP_DOWN:
    return (total_degrees - 1) - (step % total_degrees);

  case ARP_UP_DOWN: {
    Usz period = (total_degrees * 2) - 2;
    if (period < 2) period = 2;
    Usz pos = step % period;
    return pos < total_degrees ? pos : period - pos;
  }

  case ARP_DOWN_UP: {
    Usz period = (total_degrees * 2) - 2;
    if (period < 2) period = 2;
    Usz pos = step % period;
    return pos < total_degrees ? (total_degrees - 1) - pos : pos - total_degrees + 1;
  }

  case ARP_UP_DOWN_PLUS: {
    Usz period = total_degrees * 2;
    Usz pos = step % period;
    return pos < total_degrees ? pos : period - pos - 1;
  }

  case ARP_DOWN_UP_PLUS: {
    Usz period = total_degrees * 2;
    Usz pos = step % period;
    return pos < total_degrees ? total_degrees - pos - 1 : pos - total_degrees;
  }

  case ARP_CONVERGE: {
    Usz pos = step % total_degrees;
    Usz left = 0, right = total_degrees - 1;
    for (Usz i = 0; i < pos; i++) {
      if (i % 2 == 0) right--; else left++;
    }
    return (pos % 2 == 0) ? right : left;
  }

  case ARP_DIVERGE: {
    Usz pos = step % total_degrees;
    Usz mid = total_degrees / 2;
    Usz offset = pos / 2;
    if (pos % 2 == 0) {
      return (mid + offset < total_degrees) ? mid + offset : total_degrees - 1;
    } else {
      return (mid > offset) ? mid - offset - 1 : 0;
    }
  }

  case ARP_PINKY_UP: {
    if (total_degrees <= 1) return 0;
    Usz highest = total_degrees - 1;
    Usz pos = step % (total_degrees * 2 - 2);
    return (pos % 2 == 0) ? pos / 2 : highest;
  }

  case ARP_THUMB_UP: {
    if (total_degrees <= 1) return 0;
    Usz pos = step % ((total_degrees - 1) * 2);
    return (pos % 2 == 0) ? 0 : (pos / 2) + 1;
  }

  case ARP_UP_DOWN_ALT: {
    Usz half_period = total_degrees;
    Usz pos = step % (half_period * 2);
    if (pos < half_period) {
      return pos;
    } else {
      return total_degrees - 1 - (pos - half_period);
    }
  }

  case ARP_DOWN_UP_ALT: {
    Usz half_period = total_degrees;
    Usz pos = step % (half_period * 2);
    if (pos < half_period) {
      return total_degrees - 1 - pos;
    } else {
      return pos - half_period;
    }
  }

  case ARP_RANDOM: {
    Usz key = (extra_params->random_seed + step) ^ (step << 16);
    key = (key ^ 61) ^ (key >> 16);
    key = key + (key << 3);
    key = key ^ (key >> 4);
    key = key * 0x27d4eb2d;
    key = key ^ (key >> 15);
    return key % total_degrees;
  }

  case ARP_BOUNCE: {
    Usz bounce_size = total_degrees / 3;
    if (bounce_size < 1) bounce_size = 1;
    Usz pos = step % (bounce_size * 4);
    if (pos < bounce_size) return pos;
    else if (pos < bounce_size * 2) return bounce_size - 1 - (pos - bounce_size);
    else if (pos < bounce_size * 3) return pos - bounce_size * 2;
    else return bounce_size - 1 - (pos - bounce_size * 3);
  }

  default:
    return step % total_degrees;
  }
}

BEGIN_OPERATOR(arpeggiator)
  LOWERCASE_REQUIRES_BANG;
  PORT(0, 1, IN | PARAM, "Range");  // Range (1-4)
  PORT(0, 2, IN | PARAM, "Pattern");  // Pattern (0-9, a-d)
  PORT(1, 0, OUT, "");         // Degree output

  // Get inputs
  Glyph range_g = PEEK(0, 1);
  Glyph pattern_g = PEEK(0, 2);

  if (range_g == '.' || pattern_g == '.')
    return;

  // This cell's state, from the caller's op-state store (AD-5)
  Opstate_entry *entry = opstate_lookup(extra_params->ctx->opstate, y, x,
                                        This_oper_char);
  if (!entry)
    return;
  Opstate_arp *state = &entry->u.arp;

  Usz range = index_of(range_g);
  Usz pattern = index_of(pattern_g) % ARP_PATTERN_COUNT;
  if (range == 0) range = 1;
  if (range > 4) range = 4;

  // Reset counter on the first visit or if pattern or range changed
  if (!state->initialized || state->last_pattern != pattern ||
      state->last_range != range) {
    state->step_counter = 0;
    state->initialized = true;
    state->last_pattern = pattern;
    state->last_range = range;
  }

  // Get degree for current step
  Usz degree = get_arp_degree((ArpPatternType)pattern, state->step_counter, range, extra_params);

  // Output degree
  POKE(1, 0, glyph_of(degree));

  // Increment step counter for next bang
  state->step_counter++;
END_OPERATOR

// BOORCH's new Random Unique. Each lowercase r keeps its own bag in its
// op-state entry (Opstate_random) and shuffles it with its own PRNG, seeded
// from random_seed and the cell on its first visit (AD-8). It never sends one
// value twice in a row unless min equals max (B6).

// Fisher-Yates shuffle of the bag.
static void shuffle_sequence(U8 *array, Usz n, Prng *prng) {
  if (n <= 1)
    return;

  for (Usz i = n - 1; i > 0; i--) {
    Usz j = prng_bounded(prng, (U32)(i + 1));
    // Swap
    U8 temp = array[i];
    array[i] = array[j];
    array[j] = temp;
  }
}

// Shuffles the bag and starts it over. A bag that would start with the last
// value sent swaps its first slot with a random other slot, so no value
// repeats across a reshuffle or a range change; it stays a permutation. A
// bag of one value repeats it, and a zeroed entry, with no last value, never
// swaps.
static void shuffle_bag(Opstate_random *state) {
  Usz n = state->sequence_size;
  shuffle_sequence(state->sequence, n, &state->prng);
  if (n > 1 && state->has_last && state->sequence[0] == state->last_value) {
    Usz j = 1 + prng_bounded(&state->prng, (U32)(n - 1));
    U8 temp = state->sequence[0];
    state->sequence[0] = state->sequence[j];
    state->sequence[j] = temp;
  }
  state->current_index = 0;
}

static void initialize_sequence(Opstate_random *state, Usz min, Usz max) {
  Usz size = (max >= min) ? (max - min + 1) : 0;
  if (size > Opstate_random_max_size)
    size = Opstate_random_max_size;
  state->sequence_size = (U8)size;

  // Fill sequence with values from min to max
  for (Usz i = 0; i < size; i++) {
    state->sequence[i] = (U8)(min + i);
  }

  shuffle_bag(state);
}

// Modified random operator for lowercase 'r' - requires bang, uses shuffle to avoid consecutive duplicates
BEGIN_OPERATOR(random)
  // Check if this is lowercase 'r' (shuffle/unique random) or uppercase 'R' (pure random)
  if (glyph_is_lowercase(This_oper_char)) {
    // Lowercase 'r' - requires bang and uses shuffle algorithm
    LOWERCASE_REQUIRES_BANG;
    PORT(0, -1, IN | PARAM, "Min"); // Min
    PORT(0, 1, IN, "Max");          // Max
    PORT(1, 0, OUT, "");         // Output

    Glyph min_glyph = PEEK(0, -1);
    Glyph max_glyph = PEEK(0, 1);

    if (min_glyph == '.' || max_glyph == '.') {
      return;
    }

    // This cell's state, from the caller's op-state store (AD-5)
    Opstate_entry *entry = opstate_lookup(extra_params->ctx->opstate, y, x,
                                          This_oper_char);
    if (!entry)
      return;
    Opstate_random *state = &entry->u.random;

    // First visit: seed this cell's generator (AD-8)
    if (!state->initialized)
      prng_seed(&state->prng, extra_params->random_seed, y, x);

    Usz min = index_of(min_glyph);
    Usz max = index_of(max_glyph);

    if (max < min) {
      Usz temp = min;
      min = max;
      max = temp;
    }

    // Initialize or reinitialize if needed
    if (!state->initialized || state->current_index >= state->sequence_size ||
        min != (Usz)state->last_min || max != (Usz)state->last_max) {
      initialize_sequence(state, min, max);
      state->initialized = true;
      state->last_min = (U8)min;
      state->last_max = (U8)max;
    }

    // Get next value from sequence, and remember it as the last value sent
    Usz result = state->sequence[state->current_index];
    state->current_index++;
    state->last_value = (U8)result;
    state->has_last = true;

    // Reshuffle if we've used all values
    if (state->current_index >= state->sequence_size)
      shuffle_bag(state);

    // The output takes the right input's case, even when min equals max
    POKE(1, 0, glyph_with_case(glyph_of(result), max_glyph));
  } else {
    // Uppercase 'R' - pure random, evaluated every tick
    PORT(0, -1, IN | PARAM, "Min");
    PORT(0, 1, IN, "Max");
    PORT(1, 0, OUT, "");
    Glyph gb = PEEK(0, 1);
    Usz a = index_of(PEEK(0, -1));
    Usz b = index_of(gb);
    if (b == 0)
      b = 36;
    Usz min, max;
    if (a == b) {
      POKE(1, 0, glyph_of(a));
      return;
    } else if (a < b) {
      min = a;
      max = b;
    } else {
      min = b;
      max = a;
    }
    // Initial input params for the hash
    Usz key = (extra_params->random_seed + y * width + x) ^
              (Tick_number << UINT32_C(16));
    // 32-bit shift_mult hash to evenly distribute bits
    key = (key ^ UINT32_C(61)) ^ (key >> UINT32_C(16));
    key = key + (key << UINT32_C(3));
    key = key ^ (key >> UINT32_C(4));
    key = key * UINT32_C(0x27d4eb2d);
    key = key ^ (key >> UINT32_C(15));
    // Hash finished. Restrict to desired range of numbers.
    Usz val = key % (max - min) + min;
    POKE(1, 0, glyph_with_case(glyph_of(val), gb));
  }
END_OPERATOR

// BOORCH's BOUNCER OP
// Predefined waveform sequences
static char const *const waveforms[] = {
    // Triangle (0)
    "00112233445566778899aabbccddeeffgghhiijjkkllmmnnooppqqrrstuvwxyzzyxwvutsrr"
    "qqppoonnmmllkkjjiihhggffeeddccbbaa99887766554433221100",
    // Inverted Triangle (1)
    "zzyyxxwwvvuuttssrrqqppoonnmmllkkjjiihhggffeeddccbbaa9988765432100123456788"
    "99aabbccddeeffgghhiijjkkllmmnnooppqqrrssttuuvvwwxxyyzz",
    // Sine (2)
    "000000011111133333555558888cccfffiiilllooorrruuuwwwxxxyyyyyyzzzzzzzzyyyyyy"
    "xxxwwwuuurrrooollliiifffccc888855555333331111110000000",
    // Inverted Sine (3)
    "zzzzzzzyyyyyywwwwwtttttrrrrnnnkkkhhheeebbb88855533322211111100000000111111"
    "222333555888bbbeeehhhkkknnnrrrrtttttwwwwwyyyyyyzzzzzzz",
    // Square (4)
    "0000000000000000000000000000000000000000000000000000000000000000zzzzzzzzzz"
    "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
    // Inverted Square (5)
    "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz0000000000"
    "000000000000000000000000000000000000000000000000000000",
    // Saw (6)
    "0000111122223333444455556666777788889999aaaabbbbccccddddeeeeffffgggghhhhii"
    "iijjjjkkklllmmmnnnooopppqqqrrrssstttuuuvvvwwwxxxyyyzzz",
    // Inverted Saw (7)
    "zzzzyyyyxxxxwwwwvvvvuuuuttttssssrrrrqqqqppppoooonnnnmmmmllllkkkkjjjjiiiihh"
    "hhggggfffeeedddcccbbbaaa999888777666555444333222111000"};

#define WAVE_LENGTH 128

BEGIN_OPERATOR(bouncer)
  PORT(0, 1, IN | PARAM, "Start"); // Start value (a)
  PORT(0, 2, IN | PARAM, "End"); // End value (b)
  PORT(0, 3, IN | PARAM, "Rate"); // Rate (ticks per cycle)
  PORT(0, 4, IN | PARAM, "Shape"); // Shape (0-7 for different waveforms)
  PORT(1, 0, OUT, "");

  Glyph start_g = PEEK(0, 1);
  Glyph end_g = PEEK(0, 2);
  Glyph rate_g = PEEK(0, 3);
  Glyph shape_g = PEEK(0, 4);

  if (start_g == '.' || end_g == '.')
    return;

  // This cell's state, from the caller's op-state store (AD-5)
  Opstate_entry *entry = opstate_lookup(extra_params->ctx->opstate, y, x,
                                        This_oper_char);
  if (!entry)
    return;
  Opstate_bouncer *state = &entry->u.bouncer;

  Usz start = index_of(start_g);
  Usz end = index_of(end_g);
  Usz rate = index_of(rate_g);
  Usz shape = index_of(shape_g);

  // Initialize or reset on bang
  if (!state->initialized ||
      oper_has_neighboring_bang(gbuffer, height, width, y, x) ||
      rate != state->last_rate || shape != state->last_shape) {
    state->current_index = 0;
    state->initialized = true;
    state->last_rate = rate;
    state->last_shape = shape;
  }

  // Only advance if rate > 0
  if (rate > 0 && rate_g != '.') {
    state->current_index = (state->current_index + rate) % WAVE_LENGTH;
  }

  // Get raw waveform value first
  shape = shape < 8 ? shape : 0;
  char wave_value = waveforms[shape][state->current_index];

  // Get normalized position (0-1) in waveform range
  float normalized_pos = (float)index_of(wave_value) / 35.0f;

  // Map normalized position to output range
  Usz range = end > start ? end - start : start - end;
  Usz output_value = start;
  if (range > 0) {
    if (end > start) {
      output_value = start + (Usz)(normalized_pos * (float)range);
    } else {
      output_value = start - (Usz)(normalized_pos * (float)range);
    }
  }

  POKE(1, 0, glyph_of(output_value));
END_OPERATOR

//////// Run simulation

void orca_run(Glyph *restrict gbuf, Mark *restrict mbuf, Usz height, Usz width,
              Usz tick_number, Oevent_list *oevent_list, Usz random_seed,
              Orca_run_ctx const *ctx) {
  assert(ctx && ctx->opstate);
  Glyph vars_slots[Glyphs_index_count];
  memset(vars_slots, '.', sizeof(vars_slots));
  Oper_extra_params extras;
  extras.vars_slots = &vars_slots[0];
  extras.oevent_list = oevent_list;
  extras.random_seed = random_seed;
  extras.ctx = ctx;

  for (Usz iy = 0; iy < height; ++iy) {
    Glyph const *glyph_row = gbuf + iy * width;
    Mark const *mark_row = mbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      Mark cell_flags = mark_row[ix] & (Mark_flag_lock | Mark_flag_sleep);
      if (cell_flags & (Mark_flag_lock | Mark_flag_sleep))
        continue;
      switch (glyph_char) {
#define UNIQUE_CASE(_oper_char, _oper_name)                                    \
  case _oper_char:                                                             \
    oper_behavior_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number, \
                               &extras, cell_flags, glyph_char);               \
    break;

#define ALPHA_CASE(_upper_oper_char, _oper_name)                               \
  case _upper_oper_char:                                                       \
  case (char)(_upper_oper_char | 1 << 5):                                      \
    oper_behavior_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number, \
                               &extras, cell_flags, glyph_char);               \
    break;
        UNIQUE_OPERATORS(UNIQUE_CASE)
        ALPHA_OPERATORS(ALPHA_CASE)
#undef UNIQUE_CASE
#undef ALPHA_CASE
      }
    }
  }
}
