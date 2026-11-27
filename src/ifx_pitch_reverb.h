/* Intellifex LTD configuration 1: Hush; Pitch Shift; Delay; Reverb.
 *
 * The configuration at P:$00A0: the pitch network with two voices and the
 * Delay 3 voice writing the voice bus or the L/R delay lines, the reverb
 * network at $C000 fed by the input and the delay returns, and the output mix
 * of bus, delay and reverb that the ducker rides. The front, Hush and the
 * mixer are shared.
 */
#ifndef IFX_PITCH_REVERB_H
#define IFX_PITCH_REVERB_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_ducker_core.h"
#include "ifx_front_core.h"
#include "ifx_hush_core.h"
#include "ifx_pitch_core.h"
#include "ifx_process.h"
#include "ifx_reverb_net_core.h"

/* The words the configuration reads, staged by the message handlers, in the
 * configuration's parameter order. */
enum {
  IFX_PITCH_REVERB_COEF_WET = 0,        /* X:$0013: EFFECTS LVL */
  IFX_PITCH_REVERB_COEF_DIRECT_LEFT,    /* X:$0014: L DIR LVL */
  IFX_PITCH_REVERB_COEF_DIRECT_RIGHT,   /* X:$0015: R DIR LVL */
  IFX_PITCH_REVERB_COEF_FLAGS,          /* X:$0000 bits 4, 6 and 17 */
  IFX_PITCH_REVERB_COEF_PITCH_LEVEL,    /* X:$0020: PITCH LVL */
  IFX_PITCH_REVERB_COEF_DELAY_LEVEL,    /* X:$0021: DELAY LVL */
  IFX_PITCH_REVERB_COEF_REVERB_LEVEL,   /* X:$0022: REVERB LVL */
  IFX_PITCH_REVERB_COEF_REG_LEFT,       /* Y:$000F: REGEN L */
  IFX_PITCH_REVERB_COEF_REG_RIGHT,      /* Y:$0010: REGEN R */
  IFX_PITCH_REVERB_COEF_HUSH_THRESHOLD, /* X:$0006 */
  IFX_PITCH_REVERB_COEF_HUSH_SLOPE,     /* X:$0007 */
  IFX_PITCH_REVERB_COEF_HUSH_RELEASE,   /* X:$0009 */
  IFX_PITCH_REVERB_COEF_VOICES,         /* Y:$0EC0..$0ED7: two pitch voices and Delay 3 */
  IFX_PITCH_REVERB_COEF_DUCKER = IFX_PITCH_REVERB_COEF_VOICES + 3 * 8, /* Y:$002B: DUCKER */
  IFX_PITCH_REVERB_COEF_DUCK_THRESHOLD, /* Y:$0026: SENSITIVITY */
  IFX_PITCH_REVERB_COEF_DUCK_ATTEN,     /* Y:$0027: ATTENUATION */
  IFX_PITCH_REVERB_COEF_DUCK_RELEASE,   /* Y:$0028: RELEASE RATE */
  IFX_PITCH_REVERB_COEF_PAN_LEFT,       /* Y:$0008: DIR IN PAN left */
  IFX_PITCH_REVERB_COEF_PAN_RIGHT,      /* Y:$0009: DIR IN PAN right */
  IFX_PITCH_REVERB_COEF_MIX_LEFT,       /* X:$0025: MIX DIR/DLY left */
  IFX_PITCH_REVERB_COEF_MIX_RIGHT,      /* X:$0026: MIX DIR/DLY right */
  IFX_PITCH_REVERB_COEF_FEEDBACK,       /* Y:$0002: DECAY times (1 - DAMP) */
  IFX_PITCH_REVERB_COEF_DAMP,           /* Y:$0001: RV HF DAMP */
  IFX_PITCH_REVERB_COEF_COUNT
};

enum { IFX_PITCH_REVERB_FLAG_REV_INPUT = 1 << 17 }; /* X:$0000 bit 17: REV INPUT active */

typedef struct {
  int32_t coef[IFX_PITCH_REVERB_COEF_COUNT];
} ifx_pitch_reverb_config;

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  ifx_front_core_state front;
  ifx_hush_core_state hush;
  ifx_ducker_core_state ducker;
  ifx_pitch_core_state pitch;
  ifx_reverb_net_core_state reverb;
  ifx_process_block block;
} ifx_pitch_reverb_state;

void ifx_pitch_reverb_state_init(ifx_pitch_reverb_state *state,
                                 const ifx_pitch_reverb_config *config);

void ifx_pitch_reverb_process_split(ifx_pitch_reverb_state *state,
                                    const ifx_pitch_reverb_config *config, const int32_t *in,
                                    int32_t *out_l, int32_t *out_r, size_t frames);

void ifx_pitch_reverb_process_f32(ifx_pitch_reverb_state *state,
                                  const ifx_pitch_reverb_config *config, const float *in,
                                  float *out_l, float *out_r, size_t frames);

void ifx_pitch_reverb_process_interleaved(ifx_pitch_reverb_state *state,
                                          const ifx_pitch_reverb_config *config, const int32_t *in,
                                          int32_t *out, size_t frames);

void ifx_pitch_reverb_process_frame(ifx_pitch_reverb_state *state,
                                    const ifx_pitch_reverb_config *config, int32_t in,
                                    int32_t out[2]);

#endif /* IFX_PITCH_REVERB_H */
