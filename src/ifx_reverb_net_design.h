/* The reverb network's message handler math: REVERB DECAY, RV HF DAMP,
 * DIR IN PAN, MIX DIR/DLY and the feedback restage. Configurations 0 and 1
 * share it.
 */
#ifndef IFX_REVERB_NET_DESIGN_H
#define IFX_REVERB_NET_DESIGN_H

#include <stdint.h>

typedef struct {
  int32_t decay;     /* Y:$0000 */
  int32_t damp;      /* Y:$0001 */
  int32_t feedback;  /* Y:$0002 */
  int32_t pan_left;  /* Y:$0008 */
  int32_t pan_right; /* Y:$0009 */
  int32_t mix_left;  /* X:$0025 */
  int32_t mix_right; /* X:$0026 */
} ifx_reverb_net_design_words;

/* Raw values: decay and damping 0..99, pan and mix 0..100. */
void ifx_reverb_net_design_words_make(int decay, int damp, int pan, int mix,
                                      ifx_reverb_net_design_words *words);

#endif /* IFX_REVERB_NET_DESIGN_H */
