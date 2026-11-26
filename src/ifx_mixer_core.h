/* The mixer at P:$0050, shared by every configuration: wet and direct Hush
 * blocks into stereo, with the firmware's +0.05 offset on both words. */
#ifndef IFX_MIXER_CORE_H
#define IFX_MIXER_CORE_H

#include <stdint.h>

/* The firmware's constant DC offset on both outputs, which a host removes. */
#define IFX_MIXER_OFFSET 0x066666

void ifx_mixer_core_process(const int32_t wet_left[32], const int32_t wet_right[32],
                            const int32_t hush[32], int32_t wet_level, int32_t direct_left,
                            int32_t direct_right, int direct_on, int32_t out_left[32],
                            int32_t out_right[32]);

#endif /* IFX_MIXER_CORE_H */
