/* The firmware side of one effect fixture: open a board, edit knobs through the
 * MCU, read the words the handlers staged, and feed audio. */
#ifndef IFX_FIRMWARE_H
#define IFX_FIRMWARE_H

#include "ifx_effect.h"

typedef struct {
  const ifx_effect *effect;
  unsigned config; /* the MCU configuration index the fixture records */
  void *(*open)(void);
  void (*close)(void *instance);
  int (*set_knob)(void *instance, unsigned knob, int value);
  int (*load)(void *instance, const uint8_t values[108]);
  const char *(*eprom_path)(void *instance);
  int (*words)(void *instance, int32_t *words);
  int (*frame)(void *instance, int32_t in, int32_t *left, int32_t *right);
} ifx_firmware_ops;

const ifx_firmware_ops *ifx_firmware_delay(void);
const ifx_firmware_ops *ifx_firmware_reverb(void);

#endif /* IFX_FIRMWARE_H */
