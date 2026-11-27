/* The mixer at P:$0050, shared by every configuration: the wet block and the
 * Hush block as the direct signal into stereo. The firmware adds +0.05 to both
 * words before its limiter and the analog output stage removes it; the library
 * outputs the limited word minus that offset, saturated. */
#ifndef IFX_MIXER_CORE_H
#define IFX_MIXER_CORE_H

#include <stdint.h>

#include "ifx_base.h"

#define IFX_MIXER_CORE_OFFSET 0x066666

/* `direct_on` is DIRECT HUSH at POST; at PRE the direct levels play no part. */
void ifx_mixer_core_process(const int32_t wet_left[IFX_BLOCK_FRAMES],
                            const int32_t wet_right[IFX_BLOCK_FRAMES],
                            const int32_t hush[IFX_BLOCK_FRAMES], int32_t wet_level,
                            int32_t direct_left, int32_t direct_right, int direct_on,
                            int32_t out_left[IFX_BLOCK_FRAMES],
                            int32_t out_right[IFX_BLOCK_FRAMES]);

#endif /* IFX_MIXER_CORE_H */
