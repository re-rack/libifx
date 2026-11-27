/* Intellifex LTD configuration 4: Hush; 8 Voice Chorus; Delay.
 *
 * The configuration at P:$00A0: the chorus network with eight LFO voices,
 * whose L/R delay lines' oldest block is the wet signal. The front, Hush and
 * the mixer are shared.
 */
#ifndef IFX_CHORUS8_H
#define IFX_CHORUS8_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_analog_core.h"
#include "ifx_chorus_core.h"
#include "ifx_front_core.h"
#include "ifx_hush_core.h"
#include "ifx_process.h"

/* The words the configuration reads, staged by the message handlers, in the
 * configuration's parameter order. */
enum {
  IFX_CHORUS8_COEF_WET = 0,        /* X:$0013: CHORUS LVL */
  IFX_CHORUS8_COEF_DIRECT_LEFT,    /* X:$0014: L DIR LVL */
  IFX_CHORUS8_COEF_DIRECT_RIGHT,   /* X:$0015: R DIR LVL */
  IFX_CHORUS8_COEF_FLAGS,          /* X:$0000 bits 4 and 6 */
  IFX_CHORUS8_COEF_HUSH_THRESHOLD, /* X:$0006 */
  IFX_CHORUS8_COEF_HUSH_SLOPE,     /* X:$0007 */
  IFX_CHORUS8_COEF_HUSH_RELEASE,   /* X:$0009 */
  IFX_CHORUS8_COEF_REG_LEFT,       /* Y:$000F: REGEN L */
  IFX_CHORUS8_COEF_REG_RIGHT,      /* Y:$0010: REGEN R */
  IFX_CHORUS8_COEF_VOICES,         /* Y:$0EC0..$0EFF: eight voice blocks */
  IFX_CHORUS8_COEF_COUNT = IFX_CHORUS8_COEF_VOICES + 8 * 8
};

typedef struct {
  int32_t coef[IFX_CHORUS8_COEF_COUNT];
  int32_t direct_latch[2]; /* X:8101 and X:8001: the analog direct path's L and R bytes */
  int32_t output_gain;     /* OUTPUT LEVEL, 0x10000 unity */
} ifx_chorus8_config;

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  ifx_front_core_state front;
  ifx_hush_core_state hush;
  ifx_chorus_core_state chorus;
  ifx_analog_core_state analog;
  ifx_process_block block;
} ifx_chorus8_state;

void ifx_chorus8_state_init(ifx_chorus8_state *state, const ifx_chorus8_config *config);

void ifx_chorus8_process_split(ifx_chorus8_state *state, const ifx_chorus8_config *config,
                               const int32_t *in, int32_t *out_l, int32_t *out_r, size_t frames);

void ifx_chorus8_process_f32(ifx_chorus8_state *state, const ifx_chorus8_config *config,
                             const float *in, float *out_l, float *out_r, size_t frames);

void ifx_chorus8_process_interleaved(ifx_chorus8_state *state, const ifx_chorus8_config *config,
                                     const int32_t *in, int32_t *out, size_t frames);

void ifx_chorus8_process_frame(ifx_chorus8_state *state, const ifx_chorus8_config *config,
                               int32_t in, int32_t out[2]);

#endif /* IFX_CHORUS8_H */
