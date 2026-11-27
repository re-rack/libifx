/* Intellifex LTD configuration 5: Hush; Pitch Shift; Delay.
 *
 * The configuration at P:$00A0: the pitch network with four voices, whose
 * L/R delay lines' oldest block is the wet signal. The front, Hush and the
 * mixer are shared.
 */
#ifndef IFX_PITCH_H
#define IFX_PITCH_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_front_core.h"
#include "ifx_hush_core.h"
#include "ifx_pitch_core.h"
#include "ifx_process.h"

/* The words the configuration reads, staged by the message handlers, in the
 * configuration's parameter order. */
enum {
  IFX_PITCH_COEF_WET = 0,          /* X:$0013: PSHIFT LVL */
  IFX_PITCH_COEF_DIRECT_LEFT,      /* X:$0014: L DIR LVL */
  IFX_PITCH_COEF_DIRECT_RIGHT,     /* X:$0015: R DIR LVL */
  IFX_PITCH_COEF_FLAGS,            /* X:$0000 bits 4 and 6 */
  IFX_PITCH_COEF_HUSH_THRESHOLD,   /* X:$0006 */
  IFX_PITCH_COEF_HUSH_SLOPE,       /* X:$0007 */
  IFX_PITCH_COEF_HUSH_RELEASE,     /* X:$0009 */
  IFX_PITCH_COEF_REG_LEFT,         /* Y:$000F: REGEN L */
  IFX_PITCH_COEF_REG_RIGHT,        /* Y:$0010: REGEN R */
  IFX_PITCH_COEF_VOICES,           /* Y:$0EC0..$0EFF: four voice blocks */
  IFX_PITCH_COEF_COUNT = IFX_PITCH_COEF_VOICES + 4 * 8
};

typedef struct {
  int32_t coef[IFX_PITCH_COEF_COUNT];
} ifx_pitch_config;

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  ifx_front_core_state front;
  ifx_hush_core_state hush;
  ifx_pitch_core_state pitch;
  ifx_process_block block;
} ifx_pitch_state;

void ifx_pitch_state_init(ifx_pitch_state *state, const ifx_pitch_config *config);

void ifx_pitch_process_split(ifx_pitch_state *state, const ifx_pitch_config *config,
                             const int32_t *in, int32_t *out_l, int32_t *out_r, size_t frames);

void ifx_pitch_process_f32(ifx_pitch_state *state, const ifx_pitch_config *config, const float *in,
                           float *out_l, float *out_r, size_t frames);

void ifx_pitch_process_interleaved(ifx_pitch_state *state, const ifx_pitch_config *config,
                                   const int32_t *in, int32_t *out, size_t frames);

void ifx_pitch_process_frame(ifx_pitch_state *state, const ifx_pitch_config *config, int32_t in,
                             int32_t out[2]);

#endif /* IFX_PITCH_H */
