#include "ifx_firmware.h"

#include "ifx_emu.h"
#include "ifx_pitch_reverb.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ifx_emu *emu;
  int32_t values[34];
} firmware_pitch_reverb;

static const unsigned k_params[34] = {
    2,  3,  4,  5,  45, 7,  8,  97, 98, 9,  10, 11, 46,  47, 63, 15, 64,
    48, 49, 65, 20, 66, 67, 68, 69, 16, 36, 37, 38, 107, 56, 39, 42, 43,
};

static const uint8_t k_defaults[IFX_EMU_PARAMETER_COUNT] = {
    [2] = 46,  [3] = 46,  [4] = 46,  [5] = 0,    [45] = 58, [7] = 58,  [8] = 48,
    [97] = 0,  [98] = 0,  [9] = 1,   [10] = 8,   [11] = 5,  [46] = 60, [47] = 20,
    [63] = 54, [15] = 22, [64] = 1,  [48] = 120, [49] = 36, [65] = 54, [20] = 84,
    [66] = 1,  [67] = 31, [68] = 81, [69] = 160, [16] = 0,  [36] = 0,  [37] = 58,
    [38] = 0,  [107] = 1, [56] = 50, [39] = 50,  [42] = 66, [43] = 18,
};

/* The DSP words the setup record holds, in ifx_pitch_reverb_config order.
 * X:$0000 carries the runtime bits, so the adapter masks the flags away. */
static const struct {
  unsigned space;
  unsigned address;
  unsigned mask;
} k_words[IFX_PITCH_REVERB_COEF_COUNT] = {
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
    {DSP_SPACE_Y, 0x002B, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0026, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0027, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0028, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0008, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0009, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0025, 0xFFFFFFu}, {DSP_SPACE_X, 0x0026, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0002, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0001, 0xFFFFFFu},
};

static void *pitch_reverb_open(void) {
  firmware_pitch_reverb *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, 1, k_defaults)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < 34u; knob++) {
    self->values[knob] = k_defaults[k_params[knob]];
  }
  return self;
}

static int pitch_reverb_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_pitch_reverb *const self = instance;
  if (!ifx_emu_start(self->emu, 1, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < 34u; knob++) {
    self->values[knob] = values[k_params[knob]];
  }
  return 0;
}

// cppcheck-suppress constParameterCallback
static const char *pitch_reverb_eprom_path(void *instance) {
  const firmware_pitch_reverb *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

static void pitch_reverb_close(void *instance) {
  firmware_pitch_reverb *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

static int pitch_reverb_set_knob(void *instance, unsigned knob, int value) {
  firmware_pitch_reverb *const self = instance;
  if (knob >= 34u || value < 0 || !ifx_emu_send(self->emu, k_params[knob], (unsigned)value)) {
    fprintf(stderr, "pitch_reverb: send %u=%d failed\n", knob, value);
    return -1;
  }
  self->values[knob] = value;
  return 0;
}

static int32_t signed_word(uint32_t word) {
  const int32_t value = (int32_t)(word & 0xFFFFFFu);
  return value & 0x800000 ? value - 0x1000000 : value;
}

static int32_t staged_level(const ifx_dsp_board *dsp, int32_t value) {
  return signed_word(ifx_dsp_board_word(dsp, DSP_SPACE_P, 0x07BEu + ((unsigned)value & 0x3Fu)));
}

static int pitch_reverb_words(void *instance, int32_t *words) {
  firmware_pitch_reverb *const self = instance;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  for (unsigned word = 0; word < IFX_PITCH_REVERB_COEF_COUNT; word++) {
    if (word == IFX_PITCH_REVERB_COEF_REG_LEFT || word == IFX_PITCH_REVERB_COEF_REG_RIGHT) {
      continue;
    }
    words[word] = signed_word(ifx_dsp_board_word(dsp, k_words[word].space, k_words[word].address) &
                              k_words[word].mask);
  }
  /* The limiter rewrites the live regen words; the setup record keeps the T1
   * words the Regen handlers staged. */
  words[IFX_PITCH_REVERB_COEF_REG_LEFT] = staged_level(dsp, self->values[7]);
  words[IFX_PITCH_REVERB_COEF_REG_RIGHT] = staged_level(dsp, self->values[8]);
  return 0;
}

static int pitch_reverb_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_pitch_reverb *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}

static const ifx_firmware_ops k_pitch_reverb_ops = {
    .effect = &ifx_effect_pitch_reverb,
    .config = 1,
    .open = pitch_reverb_open,
    .close = pitch_reverb_close,
    .set_knob = pitch_reverb_set_knob,
    .load = pitch_reverb_load,
    .eprom_path = pitch_reverb_eprom_path,
    .words = pitch_reverb_words,
    .frame = pitch_reverb_frame,
};

const ifx_firmware_ops *ifx_firmware_pitch_reverb(void) { return &k_pitch_reverb_ops; }
