#include "ifx_firmware.h"

#include "ifx_delay.h"
#include "ifx_emu.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ifx_emu *emu;
  int32_t values[23];
} firmware_delay;

static const unsigned k_params[23] = {51, 3,  4,  5,  9,  10, 11,  24, 25, 27, 28, 29,
                                      30, 31, 32, 33, 34, 35, 100, 14, 36, 37, 38};

static const uint8_t k_defaults[IFX_EMU_PARAMETER_COUNT] = {
    [51] = 48, [3] = 46,  [4] = 46,   [5] = 0,   [9] = 1,   [10] = 8,  [11] = 5,   [24] = 1,
    [25] = 0,  [27] = 58, [28] = 0,   [29] = 75, [30] = 46, [31] = 58, [32] = 100, [33] = 50,
    [34] = 52, [35] = 0,  [100] = 12, [14] = 1,  [36] = 20, [37] = 19, [38] = 8};

/* The DSP words the setup record holds, in ifx_delay_config order. X:$0000
 * carries the two flags, so the adapter masks the runtime bits away. */
static const struct {
  unsigned space;
  unsigned address;
  unsigned mask;
} k_words[IFX_DELAY_COEF_COUNT] = {
    {DSP_SPACE_X, 0x0013, 0xFFFFFFu}, {DSP_SPACE_X, 0x0014, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0015, 0xFFFFFFu}, {DSP_SPACE_X, 0x0000, (1u << 4) | (1u << 6)},
    {DSP_SPACE_X, 0x0006, 0xFFFFFFu}, {DSP_SPACE_X, 0x0007, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0009, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0018, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0019, 0xFFFFFFu}, {DSP_SPACE_Y, 0x001A, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x001B, 0xFFFFFFu}, {DSP_SPACE_Y, 0x001E, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x001F, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0022, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0023, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0024, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0025, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0026, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0027, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0028, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x002B, 0xFFFFFFu},
};

static void *delay_open(void) {
  firmware_delay *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_load(self->emu, 3, k_defaults) ||
      !ifx_emu_settle(self->emu, NULL) || !ifx_emu_align(self->emu)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < 23u; knob++) {
    self->values[knob] = k_defaults[k_params[knob]];
  }
  return self;
}

static int delay_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_delay *const self = instance;
  if (!ifx_emu_load(self->emu, 3, values) || !ifx_emu_settle(self->emu, NULL) ||
      !ifx_emu_align(self->emu)) {
    return -1;
  }
  for (unsigned knob = 0; knob < 23u; knob++) {
    self->values[knob] = values[k_params[knob]];
  }
  return 0;
}

// cppcheck-suppress constParameterCallback
static const char *delay_eprom_path(void *instance) {
  const firmware_delay *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

static void delay_close(void *instance) {
  firmware_delay *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

static int delay_set_knob(void *instance, unsigned knob, int value) {
  firmware_delay *const self = instance;
  if (knob >= 23u || value < 0 || !ifx_emu_send(self->emu, k_params[knob], (unsigned)value)) {
    fprintf(stderr, "delay: send %u=%d failed\n", knob, value);
    return -1;
  }
  self->values[knob] = value;
  return 0;
}

static int delay_words(void *instance, int32_t *words) {
  firmware_delay *const self = instance;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  for (unsigned word = 0; word < IFX_DELAY_COEF_COUNT; word++) {
    int32_t value = (int32_t)(ifx_dsp_board_word(dsp, k_words[word].space, k_words[word].address) &
                              k_words[word].mask);
    if ((k_words[word].mask & 0xFFFFFFu) != 0u && (value & 0x800000) != 0) {
      value -= 0x1000000;
    }
    words[word] = value;
  }
  /* The 2-Tap limiter rewrites the live regen words each block; the setup record
   * keeps the T1 words the Regen handlers staged. */
  words[IFX_DELAY_COEF_REG_LEFT] =
      (int32_t)(ifx_dsp_board_word(dsp, DSP_SPACE_P, 0x07BEu + (self->values[12] & 0x3F)) &
                0xFFFFFFu);
  words[IFX_DELAY_COEF_REG_RIGHT] =
      (int32_t)(ifx_dsp_board_word(dsp, DSP_SPACE_P, 0x07BEu + (self->values[16] & 0x3F)) &
                0xFFFFFFu);
  if ((words[IFX_DELAY_COEF_REG_LEFT] & 0x800000) != 0) {
    words[IFX_DELAY_COEF_REG_LEFT] -= 0x1000000;
  }
  if ((words[IFX_DELAY_COEF_REG_RIGHT] & 0x800000) != 0) {
    words[IFX_DELAY_COEF_REG_RIGHT] -= 0x1000000;
  }
  return 0;
}

static int delay_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_delay *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}

static const ifx_firmware_ops k_delay_ops = {
    .effect = &ifx_effect_delay,
    .config = 3,
    .open = delay_open,
    .close = delay_close,
    .set_knob = delay_set_knob,
    .load = delay_load,
    .eprom_path = delay_eprom_path,
    .words = delay_words,
    .frame = delay_frame,
};

const ifx_firmware_ops *ifx_firmware_delay(void) { return &k_delay_ops; }
