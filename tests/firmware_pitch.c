#include "ifx_firmware.h"

#include "ifx_emu.h"
#include "ifx_pitch.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ifx_emu *emu;
  int32_t values[29];
} firmware_pitch;

static const unsigned k_params[29] = {
    53, 3,  4,  5,  97,  98,  9,  10, 11, 46,  47,  63, 15, 64, 48,
    49, 65, 20, 66, 101, 102, 67, 68, 69, 103, 104, 72, 73, 74,
};

static const uint8_t k_defaults[IFX_EMU_PARAMETER_COUNT] = {
    [53] = 46,   [3] = 46,   [4] = 46,  [5] = 0,     [97] = 0,   [98] = 0,  [9] = 1,    [10] = 8,
    [11] = 5,    [46] = 85,  [47] = 20, [63] = 58,   [15] = 25,  [64] = 0,  [48] = 145, [49] = 20,
    [65] = 58,   [20] = 75,  [66] = 0,  [101] = 105, [102] = 20, [67] = 58, [68] = 25,  [69] = 0,
    [103] = 145, [104] = 36, [72] = 58, [73] = 75,   [74] = 0,
};

/* The DSP words the setup record holds, in ifx_pitch_config order. X:$0000
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

static void *pitch_open(void) {
  firmware_pitch *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, 5, k_defaults)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  for (unsigned knob = 0; knob < 29u; knob++) {
    self->values[knob] = k_defaults[k_params[knob]];
  }
  return self;
}

static int pitch_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_pitch *const self = instance;
  if (!ifx_emu_start(self->emu, 5, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < 29u; knob++) {
    self->values[knob] = values[k_params[knob]];
  }
  return 0;
}

// cppcheck-suppress constParameterCallback
static const char *pitch_eprom_path(void *instance) {
  const firmware_pitch *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

static void pitch_close(void *instance) {
  firmware_pitch *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

static int pitch_set_knob(void *instance, unsigned knob, int value) {
  firmware_pitch *const self = instance;
  if (knob >= 29u || value < 0 || !ifx_emu_send(self->emu, k_params[knob], (unsigned)value)) {
    fprintf(stderr, "pitch: send %u=%d failed\n", knob, value);
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

static int pitch_words(void *instance, int32_t *words) {
  firmware_pitch *const self = instance;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  for (unsigned word = 0; word < 9u; word++) {
    words[word] = signed_word(ifx_dsp_board_word(dsp, k_words[word].space, k_words[word].address) &
                              k_words[word].mask);
  }
  /* The limiter rewrites the live regen words; the setup record keeps the T1
   * words the Regen handlers staged. */
  words[IFX_PITCH_COEF_REG_LEFT] = staged_level(dsp, self->values[4]);
  words[IFX_PITCH_COEF_REG_RIGHT] = staged_level(dsp, self->values[5]);
  for (unsigned word = 0; word < 32u; word++) {
    words[IFX_PITCH_COEF_VOICES + word] =
        signed_word(ifx_dsp_board_word(dsp, DSP_SPACE_Y, 0x0EC0u + word));
  }
  return 0;
}

static int pitch_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_pitch *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}

static const ifx_firmware_ops k_pitch_ops = {
    .effect = &ifx_effect_pitch,
    .config = 5,
    .open = pitch_open,
    .close = pitch_close,
    .set_knob = pitch_set_knob,
    .load = pitch_load,
    .eprom_path = pitch_eprom_path,
    .words = pitch_words,
    .frame = pitch_frame,
};

const ifx_firmware_ops *ifx_firmware_pitch(void) { return &k_pitch_ops; }
