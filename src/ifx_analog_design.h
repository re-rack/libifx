/* The analog stages' settings: the direct latches as C:0EE0 writes them (at
 * DIRECT HUSH PRE, L DIR LVL to X:8101 and R DIR LVL to X:8001 through the
 * curve C:31C9 reads; at POST, zero) and the OUTPUT LEVEL gain. */
#ifndef IFX_ANALOG_DESIGN_H
#define IFX_ANALOG_DESIGN_H

#include <stdint.h>

#define IFX_ANALOG_DESIGN_UNITY 0x10000

/* Raw values: the switches 0..1, level 0..58. 0 when `analog_direct` is off. */
int32_t ifx_analog_design_latch(int analog_direct, int direct_hush, int level);

/* Raw value 0..100 percent, linear, 100 exactly IFX_ANALOG_DESIGN_UNITY. */
int32_t ifx_analog_design_output(int level);

#endif /* IFX_ANALOG_DESIGN_H */
