/* The de-popped Pitch Shift, Delay and Reverb: ifx_pitch_reverb.h with its
 * turns crossfaded (ifx_alt_core.h).
 *
 * A voice, Delay 3 among them, adds into the lines at its DELAY, so a turn of
 * its DELAY, LEVEL or PAN steps the output that much later: the old settings
 * play on until then, also while the knob keeps turning. Other turns step the
 * output at once, or not at all. While the lines regenerate, a step still
 * comes round again, and a step that enters the reverb comes back out of it.
 */
#ifndef IFX_PITCH_REVERB_ALT_H
#define IFX_PITCH_REVERB_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_alt_core.h"
#include "ifx_pitch_reverb.h"

typedef ifx_pitch_reverb_config ifx_pitch_reverb_alt_config;

typedef struct {
  ifx_pitch_reverb_state exact; /* the effect, always run */
  ifx_pitch_reverb_state lane[2]; /* the frozen copies, run while heard */
  ifx_pitch_reverb_config lane_config[2]; /* the settings each copy keeps */
  ifx_pitch_reverb_config exact_config; /* the config the effect ran its last block with */
  ifx_alt_core_state alt;
} ifx_pitch_reverb_alt_state;

void ifx_pitch_reverb_alt_state_init(ifx_pitch_reverb_alt_state *state,
                                     const ifx_pitch_reverb_alt_config *config);

void ifx_pitch_reverb_alt_process_split(ifx_pitch_reverb_alt_state *state,
                                        const ifx_pitch_reverb_alt_config *config,
                                        const int32_t *in, int32_t *out_l, int32_t *out_r,
                                        size_t frames);

void ifx_pitch_reverb_alt_process_f32(ifx_pitch_reverb_alt_state *state,
                                      const ifx_pitch_reverb_alt_config *config, const float *in,
                                      float *out_l, float *out_r, size_t frames);

void ifx_pitch_reverb_alt_process_interleaved(ifx_pitch_reverb_alt_state *state,
                                              const ifx_pitch_reverb_alt_config *config,
                                              const int32_t *in, int32_t *out, size_t frames);

void ifx_pitch_reverb_alt_process_frame(ifx_pitch_reverb_alt_state *state,
                                        const ifx_pitch_reverb_alt_config *config, int32_t in,
                                        int32_t out[2]);

#endif /* IFX_PITCH_REVERB_ALT_H */
