/* Every effect the test system covers, in report order: ifx_effect_<name> in
 * tests/effect_<name>.c, ifx_firmware_<name> in a tests/firmware_*.c, and the
 * fixture tests/fixtures/<name>.bin. */
#ifndef IFX_EFFECTS_H
#define IFX_EFFECTS_H

#define IFX_EFFECTS(X) X(delay) X(chorus8) X(pitch) X(reverb) X(chorus_reverb) X(pitch_reverb)

/* The de-popped variants, each with its effect: ifx_effect_<variant> beside
 * ifx_effect_<name> in tests/effect_<name>.c, checked against the effect, with
 * no fixture of its own. */
#define IFX_ALT_EFFECTS(X)                                                                         \
  X(delay, delay_alt)                                                                              \
  X(chorus8, chorus8_alt)                                                                          \
  X(pitch, pitch_alt)                                                                              \
  X(reverb, reverb_alt)                                                                            \
  X(chorus_reverb, chorus_reverb_alt)                                                              \
  X(pitch_reverb, pitch_reverb_alt)

#endif /* IFX_EFFECTS_H */
