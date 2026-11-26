/* The Ducker message handlers' words, P:$0606..$0618: sensitivity threshold,
 * attenuation and release coefficient. */
#ifndef IFX_DUCKER_DESIGN_H
#define IFX_DUCKER_DESIGN_H

#include <stdint.h>

typedef struct {
  int32_t threshold;   /* Y:$0026 */
  int32_t attenuation; /* Y:$0027 */
  int32_t release;     /* Y:$0028 */
} ifx_ducker_design_words;

/* Raw values: sensitivity 0..24, attenuation 0..58, release 0..19. */
void ifx_ducker_design_words_make(int sensitivity, int attenuation, int release,
                                  ifx_ducker_design_words *words);

#endif /* IFX_DUCKER_DESIGN_H */
