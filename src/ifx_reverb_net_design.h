/* The reverb message handlers' math, shared by configurations 0, 1 and 2:
 * REVERB DECAY (42) and RV HF DAMP (43) with the feedback word each restages,
 * DIR IN PAN (56), and configurations 0 and 1's MIX DIR/DLY (39). */
#ifndef IFX_REVERB_NET_DESIGN_H
#define IFX_REVERB_NET_DESIGN_H

#include <stdint.h>

typedef struct {
  int32_t decay;     /* Y:$0000 */
  int32_t damp;      /* Y:$0001 */
  int32_t feedback;  /* Y:$0002: DECAY times (1 - DAMP) */
  int32_t pan_left;  /* Y:$0008 */
  int32_t pan_right; /* Y:$0009 */
} ifx_reverb_net_design_words;

/* Raw values: decay and damping 0..99, pan 0..100. */
void ifx_reverb_net_design_words_make(int decay, int damp, int pan,
                                      ifx_reverb_net_design_words *words);

/* MIX DIR/DLY 0..100 into X:$0025 and X:$0026. */
void ifx_reverb_net_design_mix(int mix, int32_t *left, int32_t *right);

#endif /* IFX_REVERB_NET_DESIGN_H */
