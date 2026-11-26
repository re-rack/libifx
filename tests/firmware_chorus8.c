#include "ifx_firmware.h"

#include "ifx_chorus8.h"
#include "ifx_emu.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ifx_emu *emu;
  int32_t values[49];
} firmware_chorus8;

static const unsigned k_params[49] = {
    52, 3,  4,  5,  97, 98, 9,  10, 11, 63, 15, 64, 17, 18, 65, 20, 66,
    22, 23, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
};

static const uint8_t k_defaults[IFX_EMU_PARAMETER_COUNT] = {
    [52] = 44,  [3] = 42,   [4] = 42,   [5] = 0,    [97] = 0,   [98] = 3,   [9] = 1,
    [10] = 8,   [11] = 5,   [63] = 58,  [15] = 0,   [64] = 5,   [17] = 41,  [18] = 25,
    [65] = 58,  [20] = 0,   [66] = 20,  [22] = 41,  [23] = 21,  [67] = 58,  [68] = 0,
    [69] = 30,  [70] = 36,  [71] = 5,   [72] = 58,  [73] = 0,   [74] = 60,  [75] = 39,
    [76] = 73,  [77] = 58,  [78] = 100, [79] = 30,  [80] = 40,  [81] = 38,  [82] = 58,
    [83] = 100, [84] = 120, [85] = 43,  [86] = 42,  [87] = 46,  [88] = 100, [89] = 140,
    [90] = 37,  [91] = 10,  [92] = 34,  [93] = 100, [94] = 160, [95] = 32,  [96] = 35,
};

/* The DSP words the setup record holds, in ifx_chorus8_config order. X:$0000
 * carries the two flags, so the adapter masks the runtime bits away. */
static const struct {
  unsigned space;
  unsigned address;
  unsigned mask;
} k_words[9] = {
    {DSP_SPACE_X, 0x0013, 0xFFFFFFu}, {DSP_SPACE_X, 0x0014, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0015, 0xFFFFFFu}, {DSP_SPACE_X, 0x0000, (1u << 4) | (1u << 6)},
    {DSP_SPACE_X, 0x0006, 0xFFFFFFu}, {DSP_SPACE_X, 0x0007, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0009, 0xFFFFFFu},
};

static void *chorus8_open(void) {
  firmware_chorus8 *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, 4, k_defaults)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < 49u; knob++) {
    self->values[knob] = k_defaults[k_params[knob]];
  }
  return self;
}

static int chorus8_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_chorus8 *const self = instance;
  if (!ifx_emu_start(self->emu, 4, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < 49u; knob++) {
    self->values[knob] = values[k_params[knob]];
  }
  return 0;
}

// cppcheck-suppress constParameterCallback
static const char *chorus8_eprom_path(void *instance) {
  const firmware_chorus8 *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

static void chorus8_close(void *instance) {
  firmware_chorus8 *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

static int chorus8_set_knob(void *instance, unsigned knob, int value) {
  firmware_chorus8 *const self = instance;
  if (knob >= 49u || value < 0 || !ifx_emu_send(self->emu, k_params[knob], (unsigned)value)) {
    fprintf(stderr, "chorus8: send %u=%d failed\n", knob, value);
    return -1;
  }
  self->values[knob] = value;
  return 0;
}

static int32_t staged_level(const ifx_dsp_board *dsp, int32_t value) {
  const int32_t word =
      (int32_t)(ifx_dsp_board_word(dsp, DSP_SPACE_P, 0x07BEu + ((unsigned)value & 0x3Fu)) &
                0xFFFFFFu);
  return word & 0x800000 ? word - 0x1000000 : word;
}

static int chorus8_words(void *instance, int32_t *words) {
  firmware_chorus8 *const self = instance;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  for (unsigned word = 0; word < 9u; word++) {
    int32_t value = (int32_t)(ifx_dsp_board_word(dsp, k_words[word].space, k_words[word].address) &
                              k_words[word].mask);
    if ((value & 0x800000) != 0) {
      value -= 0x1000000;
    }
    words[word] = value;
  }
  /* The limiter rewrites the live regen words; the setup record keeps the T1
   * words the Regen handlers staged. */
  words[IFX_CHORUS8_COEF_REG_LEFT] = staged_level(dsp, self->values[4]);
  words[IFX_CHORUS8_COEF_REG_RIGHT] = staged_level(dsp, self->values[5]);
  for (unsigned word = 0; word < 64u; word++) {
    int32_t value = (int32_t)(ifx_dsp_board_word(dsp, DSP_SPACE_Y, 0x0EC0u + word) & 0xFFFFFFu);
    if ((value & 0x800000) != 0) {
      value -= 0x1000000;
    }
    words[IFX_CHORUS8_COEF_VOICES + word] = value;
  }
  return 0;
}

static int chorus8_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_chorus8 *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}

static const ifx_firmware_ops k_chorus8_ops = {
    .effect = &ifx_effect_chorus8,
    .config = 4,
    .open = chorus8_open,
    .close = chorus8_close,
    .set_knob = chorus8_set_knob,
    .load = chorus8_load,
    .eprom_path = chorus8_eprom_path,
    .words = chorus8_words,
    .frame = chorus8_frame,
};

const ifx_firmware_ops *ifx_firmware_chorus8(void) { return &k_chorus8_ops; }
