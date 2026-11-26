#include "ifx_firmware.h"

#include "ifx_emu.h"
#include "ifx_reverb.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ifx_emu *emu;
  int32_t values[18];
} firmware_reverb;

static const unsigned k_params[18] = {50, 3,  4,  5,  9,  10, 11, 42, 43,
                                      54, 55, 56, 57, 58, 59, 60, 61, 62};

static const uint8_t k_defaults[IFX_EMU_PARAMETER_COUNT] = {
    [50] = 46,  [3] = 46,   [4] = 46,  [5] = 0,   [9] = 1,  [10] = 8,
    [11] = 5,   [42] = 81,  [43] = 42, [54] = 50, [55] = 6, [56] = 49,
    [57] = 157, [58] = 209, [59] = 0,  [60] = 0,  [61] = 0, [62] = 0,
};

/* The DSP words the setup record holds, in ifx_reverb_config order. X:$0000
 * carries the three flags, so the adapter masks the runtime bits away. */
static const struct {
  unsigned space;
  unsigned address;
  unsigned mask;
} k_words[IFX_REVERB_COEF_COUNT] = {
    {DSP_SPACE_X, 0x0013, 0xFFFFFFu}, {DSP_SPACE_X, 0x0014, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0015, 0xFFFFFFu}, {DSP_SPACE_X, 0x0000, (1u << 4) | (1u << 6) | (1u << 9)},
    {DSP_SPACE_X, 0x0006, 0xFFFFFFu}, {DSP_SPACE_X, 0x0007, 0xFFFFFFu},
    {DSP_SPACE_X, 0x0009, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0000, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0001, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0002, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x000E, 0xFFFFFFu}, {DSP_SPACE_Y, 0x000A, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0008, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0009, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0006, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0007, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0003, 0xFFFFFFu}, {DSP_SPACE_Y, 0x0004, 0xFFFFFFu},
    {DSP_SPACE_Y, 0x0005, 0xFFFFFFu},
};

static void *reverb_open(void) {
  firmware_reverb *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, 2, k_defaults)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < 18u; knob++) {
    self->values[knob] = k_defaults[k_params[knob]];
  }
  return self;
}

static int reverb_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_reverb *const self = instance;
  if (!ifx_emu_start(self->emu, 2, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < 18u; knob++) {
    self->values[knob] = values[k_params[knob]];
  }
  return 0;
}

// cppcheck-suppress constParameterCallback
static const char *reverb_eprom_path(void *instance) {
  const firmware_reverb *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

static void reverb_close(void *instance) {
  firmware_reverb *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

static int reverb_set_knob(void *instance, unsigned knob, int value) {
  firmware_reverb *const self = instance;
  if (knob >= 18u || value < 0 || !ifx_emu_send(self->emu, k_params[knob], (unsigned)value)) {
    fprintf(stderr, "reverb: send %u=%d failed\n", knob, value);
    return -1;
  }
  self->values[knob] = value;
  return 0;
}

static int reverb_words(void *instance, int32_t *words) {
  firmware_reverb *const self = instance;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  for (unsigned word = 0; word < IFX_REVERB_COEF_COUNT; word++) {
    int32_t value = (int32_t)(ifx_dsp_board_word(dsp, k_words[word].space, k_words[word].address) &
                              k_words[word].mask);
    if ((k_words[word].mask & 0xFFFFFFu) != 0u && (value & 0x800000) != 0) {
      value -= 0x1000000;
    }
    words[word] = value;
  }
  return 0;
}

static int reverb_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_reverb *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}

static const ifx_firmware_ops k_reverb_ops = {
    .effect = &ifx_effect_reverb,
    .config = 2,
    .open = reverb_open,
    .close = reverb_close,
    .set_knob = reverb_set_knob,
    .load = reverb_load,
    .eprom_path = reverb_eprom_path,
    .words = reverb_words,
    .frame = reverb_frame,
};

const ifx_firmware_ops *ifx_firmware_reverb(void) { return &k_reverb_ops; }
