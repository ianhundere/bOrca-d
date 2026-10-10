#include "tooltips.h"
#include "gbuffer.h"
#include "music.h"
#include "sim.h"
#include <stdio.h>

// Local index_of function (duplicated from sim.c)
static U8 const index_table[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //   0-15
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //  16-31
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  //  32-47
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  0,  0,  0,  0,  0,  //  48-63
    0,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //  64-79
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0,  0,  0,  0,  0,  //  80-95
    0,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //  96-111
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0,  0,  0,  0,  0}; // 112-127
static Usz index_of(Glyph c) { return index_table[c & 0x7f]; }

// Tooltip definitions for each operator
// These mirror the PORT calls in sim.c operators

static Port_tooltip midi_tooltips[] = {
  {0, 1, "Channel"},
  {0, 2, "Octave"},
  {0, 3, "Note"},
  {0, 4, "Velocity"},
  {0, 5, "Length"}
};

static Port_tooltip midicc_tooltips[] = {
  {0, 1, "Channel"},
  {0, 2, "Control (hundreds)"},
  {0, 3, "Control (tens)"},
  {0, 4, "Control (ones)"},
  {0, 5, "Value"},
  {0, 6, "Interpolation Rate"}
};

static Port_tooltip midipb_tooltips[] = {
  {0, 1, "Channel"},
  {0, 2, "MSB"},
  {0, 3, "LSB"}
};

static Port_tooltip scale_tooltips[] = {
  {0, 1, "Octave"},
  {0, 2, "Root"},
  {0, 3, "Scale"},
  {0, 4, "Degree"}
};

static Port_tooltip midichord_tooltips[] = {
  {0, 1, "Channel"},
  {0, 2, "Octave"},
  {0, 3, "Root note"},
  {0, 4, "Chord type"},
  {0, 5, "Velocity"},
  {0, 6, "Length"}
};

static Port_tooltip arpeggiator_tooltips[] = {
  {0, 1, "Range"},
  {0, 2, "Pattern"}
};

static Port_tooltip bouncer_tooltips[] = {
  {0, 1, "Start"},
  {0, 2, "End"},
  {0, 3, "Rate"},
  {0, 4, "Shape"}
};

static Port_tooltip add_tooltips[] = {
  {0, -1, "Value A"},
  {0, 1, "Value B"}
};

static Port_tooltip subtract_tooltips[] = {
  {0, -1, "Value A"},
  {0, 1, "Value B"}
};

static Port_tooltip multiply_tooltips[] = {
  {0, -1, "Factor A"},
  {0, 1, "Factor B"}
};

static Port_tooltip clock_tooltips[] = {
  {0, -1, "Rate"},
  {0, 1, "Modulo"}
};

static Port_tooltip delay_tooltips[] = {
  {0, -1, "Rate"},
  {0, 1, "Modulo"}
};

static Port_tooltip if_tooltips[] = {
  {0, -1, "Value A"},
  {0, 1, "Value B"}
};

static Port_tooltip generator_tooltips[] = {
  {0, -3, "X offset"},
  {0, -2, "Y offset"},
  {0, -1, "Length"}
};

static Port_tooltip halt_tooltips[] = {
  {1, 0, "Input"}
};

static Port_tooltip increment_tooltips[] = {
  {0, -1, "Rate"},
  {0, 1, "Max"}
};

static Port_tooltip jump_tooltips[] = {
  {-1, 0, "Input"}
};

static Port_tooltip konkat_tooltips[] = {
  {0, -1, "Length"}
};

static Port_tooltip lesser_tooltips[] = {
  {0, -1, "Value A"},
  {0, 1, "Value B"}
};

static Port_tooltip offset_tooltips[] = {
  {0, -2, "X offset"},
  {0, -1, "Y offset"}
};

static Port_tooltip push_tooltips[] = {
  {0, -2, "Key"},
  {0, -1, "Length"},
  {0, 1, "Input"}
};

static Port_tooltip query_tooltips[] = {
  {0, -3, "X offset"},
  {0, -2, "Y offset"},
  {0, -1, "Length"}
};

static Port_tooltip random_tooltips[] = {
  {0, -1, "Min"},
  {0, 1, "Max"}
};

static Port_tooltip track_tooltips[] = {
  {0, -2, "Key"},
  {0, -1, "Length"}
};

static Port_tooltip uclid_tooltips[] = {
  {0, -1, "Steps"},
  {0, 1, "Max"}
};

static Port_tooltip variable_tooltips[] = {
  {0, -1, "Variable"},
  {0, 1, "Value"}
};

static Port_tooltip teleport_tooltips[] = {
  {0, -2, "X offset"},
  {0, -1, "Y offset"},
  {0, 1, "Input"}
};

static Port_tooltip yump_tooltips[] = {
  {0, -1, "Input"}
};

static Port_tooltip lerp_tooltips[] = {
  {0, -1, "Rate"},
  {0, 1, "Target"}
};

// Master operator tooltips table
static Operator_tooltips operator_tooltips_table[] = {
  {':', midi_tooltips, ORCA_ARRAY_COUNTOF(midi_tooltips)},
  {'%', midi_tooltips, ORCA_ARRAY_COUNTOF(midi_tooltips)}, // mono uses same ports
  {'!', midicc_tooltips, ORCA_ARRAY_COUNTOF(midicc_tooltips)},
  {'?', midipb_tooltips, ORCA_ARRAY_COUNTOF(midipb_tooltips)},
  {'$', scale_tooltips, ORCA_ARRAY_COUNTOF(scale_tooltips)},
  {'=', midichord_tooltips, ORCA_ARRAY_COUNTOF(midichord_tooltips)},
  {';', arpeggiator_tooltips, ORCA_ARRAY_COUNTOF(arpeggiator_tooltips)},
  {'&', bouncer_tooltips, ORCA_ARRAY_COUNTOF(bouncer_tooltips)},
  {'A', add_tooltips, ORCA_ARRAY_COUNTOF(add_tooltips)},
  {'a', add_tooltips, ORCA_ARRAY_COUNTOF(add_tooltips)},
  {'B', subtract_tooltips, ORCA_ARRAY_COUNTOF(subtract_tooltips)},
  {'b', subtract_tooltips, ORCA_ARRAY_COUNTOF(subtract_tooltips)},
  {'C', clock_tooltips, ORCA_ARRAY_COUNTOF(clock_tooltips)},
  {'c', clock_tooltips, ORCA_ARRAY_COUNTOF(clock_tooltips)},
  {'D', delay_tooltips, ORCA_ARRAY_COUNTOF(delay_tooltips)},
  {'d', delay_tooltips, ORCA_ARRAY_COUNTOF(delay_tooltips)},
  {'F', if_tooltips, ORCA_ARRAY_COUNTOF(if_tooltips)},
  {'f', if_tooltips, ORCA_ARRAY_COUNTOF(if_tooltips)},
  {'G', generator_tooltips, ORCA_ARRAY_COUNTOF(generator_tooltips)},
  {'g', generator_tooltips, ORCA_ARRAY_COUNTOF(generator_tooltips)},
  {'H', halt_tooltips, ORCA_ARRAY_COUNTOF(halt_tooltips)},
  {'h', halt_tooltips, ORCA_ARRAY_COUNTOF(halt_tooltips)},
  {'I', increment_tooltips, ORCA_ARRAY_COUNTOF(increment_tooltips)},
  {'i', increment_tooltips, ORCA_ARRAY_COUNTOF(increment_tooltips)},
  {'J', jump_tooltips, ORCA_ARRAY_COUNTOF(jump_tooltips)},
  {'j', jump_tooltips, ORCA_ARRAY_COUNTOF(jump_tooltips)},
  {'K', konkat_tooltips, ORCA_ARRAY_COUNTOF(konkat_tooltips)},
  {'k', konkat_tooltips, ORCA_ARRAY_COUNTOF(konkat_tooltips)},
  {'L', lesser_tooltips, ORCA_ARRAY_COUNTOF(lesser_tooltips)},
  {'l', lesser_tooltips, ORCA_ARRAY_COUNTOF(lesser_tooltips)},
  {'M', multiply_tooltips, ORCA_ARRAY_COUNTOF(multiply_tooltips)},
  {'m', multiply_tooltips, ORCA_ARRAY_COUNTOF(multiply_tooltips)},
  {'O', offset_tooltips, ORCA_ARRAY_COUNTOF(offset_tooltips)},
  {'o', offset_tooltips, ORCA_ARRAY_COUNTOF(offset_tooltips)},
  {'P', push_tooltips, ORCA_ARRAY_COUNTOF(push_tooltips)},
  {'p', push_tooltips, ORCA_ARRAY_COUNTOF(push_tooltips)},
  {'Q', query_tooltips, ORCA_ARRAY_COUNTOF(query_tooltips)},
  {'q', query_tooltips, ORCA_ARRAY_COUNTOF(query_tooltips)},
  {'R', random_tooltips, ORCA_ARRAY_COUNTOF(random_tooltips)},
  {'r', random_tooltips, ORCA_ARRAY_COUNTOF(random_tooltips)},
  {'T', track_tooltips, ORCA_ARRAY_COUNTOF(track_tooltips)},
  {'t', track_tooltips, ORCA_ARRAY_COUNTOF(track_tooltips)},
  {'U', uclid_tooltips, ORCA_ARRAY_COUNTOF(uclid_tooltips)},
  {'u', uclid_tooltips, ORCA_ARRAY_COUNTOF(uclid_tooltips)},
  {'V', variable_tooltips, ORCA_ARRAY_COUNTOF(variable_tooltips)},
  {'v', variable_tooltips, ORCA_ARRAY_COUNTOF(variable_tooltips)},
  {'X', teleport_tooltips, ORCA_ARRAY_COUNTOF(teleport_tooltips)},
  {'x', teleport_tooltips, ORCA_ARRAY_COUNTOF(teleport_tooltips)},
  {'Y', yump_tooltips, ORCA_ARRAY_COUNTOF(yump_tooltips)},
  {'y', yump_tooltips, ORCA_ARRAY_COUNTOF(yump_tooltips)},
  {'Z', lerp_tooltips, ORCA_ARRAY_COUNTOF(lerp_tooltips)},
  {'z', lerp_tooltips, ORCA_ARRAY_COUNTOF(lerp_tooltips)}
};

char const *get_tooltip_at_cursor(Glyph const *gbuffer, Mark const *mbuffer,
                                  Usz field_h, Usz field_w, 
                                  Usz cursor_y, Usz cursor_x) {
  Enhanced_tooltip enhanced = get_enhanced_tooltip_at_cursor(gbuffer, mbuffer, field_h, field_w, cursor_y, cursor_x);
  if (enhanced.is_enhanced) {
    // For enhanced tooltips, create a single-line version for backward compatibility
    static char single_line_tooltip[64];
    snprintf(single_line_tooltip, sizeof(single_line_tooltip), "%s %s", enhanced.line1, enhanced.line2);
    return single_line_tooltip;
  }
  return enhanced.line1; // For regular tooltips, line1 contains the tooltip text
}

Enhanced_tooltip get_enhanced_tooltip_at_cursor(Glyph const *gbuffer, Mark const *mbuffer,
                                                Usz field_h, Usz field_w, 
                                                Usz cursor_y, Usz cursor_x) {
  Enhanced_tooltip result = {NULL, NULL, false};
  
  // Check bounds
  if (cursor_y >= field_h || cursor_x >= field_w)
    return result;
    
  // Check if cursor is on a PORT
  Mark cursor_mark = mbuffer[cursor_y * field_w + cursor_x];
  if (!(cursor_mark & (Mark_flag_input | Mark_flag_output)))
    return result;
    
  // Get the glyph at cursor position
  Glyph cursor_glyph = gbuffer[cursor_y * field_w + cursor_x];
    
  // Search for operator that owns this PORT
  // We need to look in the surrounding area for an operator character
  for (Isz dy = -10; dy <= 10; dy++) {
    for (Isz dx = -10; dx <= 10; dx++) {
      Isz op_y = (Isz)cursor_y + dy;
      Isz op_x = (Isz)cursor_x + dx;
      
      // Check bounds for operator position
      if (op_y < 0 || op_x < 0 || (Usz)op_y >= field_h || (Usz)op_x >= field_w)
        continue;
        
      Glyph op_char = gbuffer[(Usz)op_y * field_w + (Usz)op_x];
      
      // Skip if not an operator character or is a dot
      if (op_char == '.' || op_char == ' ')
        continue;
        
      // Find operator in tooltips table
      for (Usz i = 0; i < ORCA_ARRAY_COUNTOF(operator_tooltips_table); i++) {
        Operator_tooltips const *op_tooltips = &operator_tooltips_table[i];
        if (op_tooltips->operator_char != op_char)
          continue;
          
        // Check if cursor position matches any PORT relative to this operator
        Isz rel_y = (Isz)cursor_y - op_y;
        Isz rel_x = (Isz)cursor_x - op_x;
        
        for (Usz j = 0; j < op_tooltips->port_count; j++) {
          Port_tooltip const *port = &op_tooltips->ports[j];
          if (port->delta_y == rel_y && port->delta_x == rel_x) {
            // Check for special scale/chord tooltip enhancement
            bool is_scale_chord_port = false;
            bool is_midichord_op = (op_char == '=');
            
            // Check if this is a scale/chord input port
            if ((op_char == '$' && rel_y == 0 && rel_x == 3) ||  // Scale operator, scale/chord input
                (op_char == '=' && rel_y == 0 && rel_x == 4)) {  // Midichord operator, chord input
              is_scale_chord_port = true;
            }
            
            // If it's a scale/chord port and has a non-empty value, enhance the tooltip
            if (is_scale_chord_port && cursor_glyph != '.') {
              // The selector's name from music.h; any glyph but 0-9, a-z
              // and A-Z has none. Music_name_max bytes hold every name.
              static char scale_chord_name[Music_name_max];
              Music_op music_op = is_midichord_op ? Music_op_midichord : Music_op_scale;
              if (music_selector_name(music_op, cursor_glyph, scale_chord_name,
                                      sizeof scale_chord_name)) {
                // Create enhanced two-line tooltip
                char const *label;
                if (is_midichord_op) {
                  label = "Chord type:";
                } else {
                  // For Scale operator, distinguish between scales (0-9) and chords (a-z, A-Z)
                  if (cursor_glyph >= '0' && cursor_glyph <= '9') {
                    label = "Scale:";
                  } else {
                    label = "Chord:";
                  }
                }
                result.line1 = label;
                result.line2 = scale_chord_name;
                result.is_enhanced = true;
                return result;
              }
            }
            
            // Check for MIDI CC value port enhancement
            bool is_midicc_value_port = (op_char == '!' && rel_y == 0 && rel_x == 5);  // MIDI CC operator, value input
            bool is_midicc_interp_port = (op_char == '!' && rel_y == 0 && rel_x == 6);  // MIDI CC operator, interpolation rate input
            
            // If it's a MIDI CC value port and has a non-empty value, enhance the tooltip
            if (is_midicc_value_port && cursor_glyph != '.') {
              static char value_text[16];
              Usz value_index = index_of(cursor_glyph);
              Usz midi_value = value_index * 4;
              if (midi_value > 127) midi_value = 127;  // Clamp to MIDI CC range
              snprintf(value_text, sizeof(value_text), "%d", (int)midi_value);
              
              result.line1 = "Value:";
              result.line2 = value_text;
              result.is_enhanced = true;
              return result;
            }
            
            // If it's a MIDI CC interpolation rate port and has a non-empty value, enhance the tooltip
            if (is_midicc_interp_port && cursor_glyph != '.') {
              static char interp_text[32];
              if (cursor_glyph == '.') {
                snprintf(interp_text, sizeof(interp_text), "instant");
              } else {
                Usz rate_index = index_of(cursor_glyph);
                snprintf(interp_text, sizeof(interp_text), "rate %d", (int)rate_index);
              }
              
              result.line1 = "Interpolation:";
              result.line2 = interp_text;
              result.is_enhanced = true;
              return result;
            }
            
            // Regular tooltip
            result.line1 = port->tooltip;
            result.line2 = NULL;
            result.is_enhanced = false;
            return result;
          }
        }
      }
    }
  }
  
  return result;
}
