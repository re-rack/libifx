/* Regeneration limiting: while the sum of the regeneration gains reaches 1.0,
 * both live gain words are scaled by 0.8889 in place. */
#ifndef IFX_REGEN_CORE_H
#define IFX_REGEN_CORE_H

#include <stdint.h>

void ifx_regen_core_limit(int32_t *left, int32_t *right);

#endif /* IFX_REGEN_CORE_H */
