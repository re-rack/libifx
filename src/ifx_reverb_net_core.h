/* The reverb network configurations 0 and 1 share: the 14 pointer/coefficient
 * pairs at $C000, the two 2-word allpass input lines, and four tap/damped
 * allpass stages alternating left and right. */
#ifndef IFX_REVERB_NET_CORE_H
#define IFX_REVERB_NET_CORE_H

#include <stdint.h>

#define IFX_REVERB_NET_TABLE_WORDS 28u
#define IFX_REVERB_NET_M 0x3FBFu

typedef struct {
  int32_t table[IFX_REVERB_NET_TABLE_WORDS]; /* X:$1080..$109B, advanced in place */
  int32_t work_l[32]; /* Y:$40..$5F */
  int32_t work_r[32]; /* Y:$60..$7F */
} ifx_reverb_net_state;

void ifx_reverb_net_state_init(ifx_reverb_net_state *state);

/* One block: the Hush block, the delay-line taps at `delay_l`/`delay_r`, the
 * input pan (Y:$0008/$0009), the DIR/DLY mix (X:$0025/$0026), the ducker's
 * gain and delay bit, the REV INPUT flag, and the RV HF DAMP and feedback
 * words. */
void ifx_reverb_net_process(ifx_reverb_net_state *state, int32_t memory[0x10000],
                            const int32_t input[32], uint32_t delay_l, uint32_t delay_r,
                            int32_t pan_l, int32_t pan_r, int32_t mix_l, int32_t mix_r,
                            int32_t ducker_gain, int ducker_delay, int rev_input, int32_t damp,
                            int32_t feedback, int32_t out_l[32], int32_t out_r[32]);

#endif /* IFX_REVERB_NET_CORE_H */
