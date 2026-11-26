#include "ifx_firmware.h"

#include "ifx_chorus_reverb.h"
#include "ifx_emu.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ifx_emu *emu;
  int32_t values[41];
} firmware_chorus_reverb;

static const unsigned k_params[41] = {
    2,  3,  4,  5,  6,  7,  8,  97, 98, 9,  10, 11, 63, 15, 64, 17,  18, 65, 20, 66, 22,
    23, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 16, 36, 37, 38, 107, 56, 39, 42, 43,
};

static const uint8_t k_defaults[IFX_EMU_PARAMETER_COUNT] = {
    [2] = 46,   [3] = 46,   [4] = 46,  [5] = 0,   [6] = 58,   [7] = 32,  [8] = 58,
    [97] = 0,   [98] = 41,  [9] = 1,   [10] = 8,  [11] = 5,   [63] = 58, [15] = 0,
    [64] = 0,   [17] = 36,  [18] = 12, [65] = 58, [20] = 100, [66] = 0,  [22] = 40,
    [23] = 30,  [67] = 58,  [68] = 0,  [69] = 88, [70] = 0,   [71] = 0,  [72] = 58,
    [73] = 100, [74] = 176, [75] = 0,  [76] = 0,  [16] = 0,   [36] = 0,  [37] = 58,
    [38] = 0,   [107] = 1,  [56] = 30, [39] = 10, [42] = 71,  [43] = 18,
};

/* The DSP words the setup record holds, in ifx_chorus_reverb_config order.
 * X:$0000 carries the runtime bits, so the adapter masks the flags away. */
static const struct {
  unsigned space;
  unsigned address;
  unsigned mask;
} k_words[IFX_CHORUS_REVERB_COEF_COUNT] = {
    {DSP_SPACE_X, 0x0013, 0xFFFFFFu}, {DSP_SPACE_X, 0x0014, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0015, 0xFFFFFFu}, {DSP_SPACE_X, 0x0000, (1u << 4) | (1u << 6) | (1u << 17)},
    {DSP_SPACE_X, 0x0020, 0xFFFFFFu}, {DSP_SPACE_X, 0x0021, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0022, 0xFFFFFFu}, {DSP_SPACE_Y, 0x000F, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0010, 0xFFFFFFu}, {DSP_SPACE_X, 0x0006, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0007, 0xFFFFFFu}, {DSP_SPACE_X, 0x0009, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EC0, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EC1, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EC2, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EC3, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EC4, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EC5, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EC6, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EC7, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EC8, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EC9, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ECA, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ECB, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ECC, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ECD, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ECE, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ECF, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ED0, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ED1, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ED2, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ED3, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ED4, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ED5, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ED6, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ED7, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0ED8, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0ED9, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EDA, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EDB, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EDC, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EDD, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0EDE, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0EDF, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x002B, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0026, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0027, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0028, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0008, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0009, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0025, 0xFFFFFFu}, {DSP_SPACE_X, 0x0026, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0002, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0001, 0xFFFFFFu},
};

static void *chorus_reverb_open(void) {
  firmware_chorus_reverb *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, 0, k_defaults)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < 41u; knob++) {
    self->values[knob] = k_defaults[k_params[knob]];
  }
  return self;
}

static int chorus_reverb_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_chorus_reverb *const self = instance;
  if (!ifx_emu_start(self->emu, 0, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < 41u; knob++) {
    self->values[knob] = values[k_params[knob]];
  }
  return 0;
}

// cppcheck-suppress constParameterCallback
static const char *chorus_reverb_eprom_path(void *instance) {
  const firmware_chorus_reverb *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

static void chorus_reverb_close(void *instance) {
  firmware_chorus_reverb *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

static int chorus_reverb_set_knob(void *instance, unsigned knob, int value) {
  firmware_chorus_reverb *const self = instance;
  if (knob >= 41u || value < 0 || !ifx_emu_send(self->emu, k_params[knob], (unsigned)value)) {
    fprintf(stderr, "chorus_reverb: send %u=%d failed\n", knob, value);
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

static int chorus_reverb_words(void *instance, int32_t *words) {
  firmware_chorus_reverb *const self = instance;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  for (unsigned word = 0; word < IFX_CHORUS_REVERB_COEF_COUNT; word++) {
    if (word == IFX_CHORUS_REVERB_COEF_REG_LEFT || word == IFX_CHORUS_REVERB_COEF_REG_RIGHT) {
      continue;
    }
    int32_t value = (int32_t)(ifx_dsp_board_word(dsp, k_words[word].space, k_words[word].address) &
                              k_words[word].mask);
    if ((value & 0x800000) != 0) {
      value -= 0x1000000;
    }
    words[word] = value;
  }
  /* The limiter rewrites the live regen words; the setup record keeps the T1
   * words the Regen handlers staged. */
  words[IFX_CHORUS_REVERB_COEF_REG_LEFT] = staged_level(dsp, self->values[7]);
  words[IFX_CHORUS_REVERB_COEF_REG_RIGHT] = staged_level(dsp, self->values[8]);
  return 0;
}

static int chorus_reverb_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_chorus_reverb *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}

static const ifx_firmware_ops k_chorus_reverb_ops = {
    .effect = &ifx_effect_chorus_reverb,
    .config = 0,
    .open = chorus_reverb_open,
    .close = chorus_reverb_close,
    .set_knob = chorus_reverb_set_knob,
    .load = chorus_reverb_load,
    .eprom_path = chorus_reverb_eprom_path,
    .words = chorus_reverb_words,
    .frame = chorus_reverb_frame,
};

const ifx_firmware_ops *ifx_firmware_chorus_reverb(void) { return &k_chorus_reverb_ops; }
