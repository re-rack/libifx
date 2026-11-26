/* The Hush message handlers' math, P:$0569 and P:$0577: the threshold and its
 * matching slope, and the release coefficient. */
#ifndef IFX_HUSH_DESIGN_H
#define IFX_HUSH_DESIGN_H

#include <stdint.h>

typedef struct {
  int32_t threshold; /* X:$0006 */
  int32_t slope;     /* X:$0007 */
  int32_t release;   /* X:$0009 */
} ifx_hush_design_words;

/* Raw values are the panel's: threshold 0..24, release 0..9. */
void ifx_hush_design_words_make(int threshold, int release, ifx_hush_design_words *words);

#endif /* IFX_HUSH_DESIGN_H */
