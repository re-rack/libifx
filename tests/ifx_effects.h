/* Every effect the test system covers, in report order: ifx_effect_<name> in
 * tests/effect_<name>.c, ifx_firmware_<name> in a tests/firmware_*.c, and the
 * fixture tests/fixtures/<name>.bin. */
#ifndef IFX_EFFECTS_H
#define IFX_EFFECTS_H

#define IFX_EFFECTS(X) X(delay) X(chorus8) X(reverb)

#endif /* IFX_EFFECTS_H */
