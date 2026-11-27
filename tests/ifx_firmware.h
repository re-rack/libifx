/* The firmware half of one effect fixture: the configuration and the DSP words
 * its setup record holds, in ifx_<effect>_config order. ifx_firmware.c boots the
 * harness from this description, edits knobs through the MCU and reads back the
 * words the handlers staged. */
#ifndef IFX_FIRMWARE_H
#define IFX_FIRMWARE_H

#include "ifx_effect.h"
#include "ifx_emu.h"

#include "hatari/dsp_cpu.h"

#include <stdint.h>

typedef enum {
  IFX_FIRMWARE_WORDS, /* `count` words from `address` */
  IFX_FIRMWARE_STAGED /* the T1 entry at P:$07BE a knob's value selects */
} ifx_firmware_source;

typedef struct {
  ifx_firmware_source source;
  unsigned space; /* DSP_SPACE_X, _Y or _P */
  uint32_t address;
  uint32_t count;
  uint32_t mask; /* 0xFFFFFF: the whole word, sign extended */
  unsigned knob;
} ifx_firmware_word;

#define IFX_FIRMWARE_X(address) {IFX_FIRMWARE_WORDS, DSP_SPACE_X, (address), 1u, 0xFFFFFFu, 0u}
#define IFX_FIRMWARE_Y(address) {IFX_FIRMWARE_WORDS, DSP_SPACE_Y, (address), 1u, 0xFFFFFFu, 0u}
/* X:$0000 also carries runtime bits; only the flags the handlers stage count. */
#define IFX_FIRMWARE_FLAGS(mask) {IFX_FIRMWARE_WORDS, DSP_SPACE_X, 0x0000u, 1u, (mask), 0u}
#define IFX_FIRMWARE_Y_RUN(address, count)                                                         \
  {IFX_FIRMWARE_WORDS, DSP_SPACE_Y, (address), (count), 0xFFFFFFu, 0u}
/* A Regen word: the limiter rewrites the live word, so the setup record keeps
 * the T1 entry the knob staged. */
#define IFX_FIRMWARE_REGEN(knob) {IFX_FIRMWARE_STAGED, DSP_SPACE_P, 0x07BEu, 1u, 0xFFFFFFu, (knob)}

typedef struct {
  const ifx_effect *effect;
  unsigned config;
  const ifx_firmware_word *words;
  unsigned word_count;
} ifx_firmware;

void *ifx_firmware_open(const ifx_firmware *firmware);
void ifx_firmware_close(void *instance);
int ifx_firmware_set_knob(void *instance, unsigned knob, int value);
int ifx_firmware_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]);
const ifx_emu *ifx_firmware_emu(const void *instance);
int ifx_firmware_words(void *instance, int32_t *words);
int ifx_firmware_frame(void *instance, int32_t in, int32_t *left, int32_t *right);

#define IFX_FIRMWARE_DECLARE(name) extern const ifx_firmware ifx_firmware_##name;
IFX_EFFECTS(IFX_FIRMWARE_DECLARE)
#undef IFX_FIRMWARE_DECLARE

#endif /* IFX_FIRMWARE_H */
