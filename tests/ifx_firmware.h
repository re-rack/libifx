/* The firmware half of one effect fixture: the configuration and the DSP words
 * its setup record holds, in ifx_<effect>_config order. ifx_firmware.c boots the
 * harness from this description, edits knobs through the MCU and reads back the
 * words the handlers staged. */
#ifndef IFX_FIRMWARE_H
#define IFX_FIRMWARE_H

#include "ifx_effect.h"

#include <stdint.h>

/* Where one setup word comes from: a single masked word, a run of consecutive
 * words, or the T1 entry a Regen knob selects at P:$07BE. */
typedef struct {
  uint8_t kind;     /* IFX_FW_WORD, _RANGE or _STAGED */
  uint8_t space;    /* DSP space for WORD and RANGE */
  uint16_t address; /* first address for WORD and RANGE */
  uint16_t count;   /* words in a RANGE */
  uint32_t mask;    /* WORD's mask, 0xFFFFFF when the whole word is kept */
  uint8_t knob;     /* STAGED: the Regen knob whose value selects the entry */
} ifx_firmware_word;

enum { IFX_FW_WORD, IFX_FW_RANGE, IFX_FW_STAGED };

typedef struct {
  const ifx_effect *effect;
  unsigned config;
  const ifx_firmware_word *words; /* the setup record's words, in config order */
  unsigned word_count;
} ifx_firmware_ops;

void *ifx_firmware_open(const ifx_firmware_ops *ops);
void ifx_firmware_close(void *instance);
int ifx_firmware_set_knob(void *instance, unsigned knob, int value);
int ifx_firmware_load(void *instance, const uint8_t *values);
const char *ifx_firmware_eprom_path(const void *instance);
int ifx_firmware_words(void *instance, int32_t *words);
int ifx_firmware_frame(void *instance, int32_t in, int32_t *left, int32_t *right);

#define IFX_FIRMWARE_DECLARE(name) extern const ifx_firmware_ops ifx_firmware_##name;
IFX_EFFECTS(IFX_FIRMWARE_DECLARE)
#undef IFX_FIRMWARE_DECLARE

#endif /* IFX_FIRMWARE_H */
