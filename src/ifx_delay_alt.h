/* The de-popped Delay: ifx_delay.h with its turns crossfaded (ifx_alt_core.h).
 *
 * Most turns step the output at once, and the crossfade hides the step: DLY
 * TIME moves the taps, a level, pan, REGEN or DUCKER turn a gain. While the
 * lines regenerate, the step a turn writes into them still comes round a tap
 * later. DELAY with a MUTE TYPE that cuts the input steps what the lines take
 * in, which reaches the output a tap later, and D TYPE wipes the lines: the
 * old settings play on until then.
 */
#ifndef IFX_DELAY_ALT_H
#define IFX_DELAY_ALT_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_alt_core.h"
#include "ifx_delay.h"

typedef ifx_delay_config ifx_delay_alt_config;

typedef struct {
  ifx_delay_state exact; /* the effect, always run */
  ifx_delay_state lane[2]; /* the frozen copies, run while heard */
  ifx_delay_config lane_config[2]; /* the settings each copy keeps */
  ifx_delay_config exact_config; /* the config the effect ran its last block with */
  ifx_alt_core_state alt;
} ifx_delay_alt_state;

void ifx_delay_alt_state_init(ifx_delay_alt_state *state, const ifx_delay_alt_config *config);

void ifx_delay_alt_process_split(ifx_delay_alt_state *state, const ifx_delay_alt_config *config,
                                 const int32_t *in, int32_t *out_l, int32_t *out_r, size_t frames);

void ifx_delay_alt_process_f32(ifx_delay_alt_state *state, const ifx_delay_alt_config *config,
                               const float *in, float *out_l, float *out_r, size_t frames);

void ifx_delay_alt_process_interleaved(ifx_delay_alt_state *state,
                                       const ifx_delay_alt_config *config, const int32_t *in,
                                       int32_t *out, size_t frames);

void ifx_delay_alt_process_frame(ifx_delay_alt_state *state, const ifx_delay_alt_config *config,
                                 int32_t in, int32_t out[2]);

#endif /* IFX_DELAY_ALT_H */
