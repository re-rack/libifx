#include "ifx_firmware.h"

#include "ifx_dsp_math.h"
#include "ifx_emu.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>

/* The recorder's knob tables never exceed this; the value mirrors setup_vectors. */
enum { IFX_FIRMWARE_MAX_KNOBS = 64, IFX_FIRMWARE_STAGED_TABLE = 0x07BE };

typedef struct {
  const ifx_firmware_ops *ops;
  ifx_emu *emu;
  int32_t values[IFX_FIRMWARE_MAX_KNOBS];
} firmware_instance;

void *ifx_firmware_open(const ifx_firmware_ops *ops) {
  firmware_instance *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->ops = ops;
  uint8_t defaults[IFX_EMU_PARAMETER_COUNT] = {0};
  for (unsigned knob = 0; knob < ops->effect->knobs; knob++) {
    const ifx_effect_knob *record = &ops->effect->knob[knob];
    defaults[record->param] = (uint8_t)record->def;
    self->values[knob] = record->def;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, ops->config, defaults)) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
    return NULL;
  }
  return self;
}

void ifx_firmware_close(void *instance) {
  firmware_instance *const self = instance;
  if (self != NULL) {
    ifx_emu_destroy(self->emu);
    free(self);
  }
}

int ifx_firmware_set_knob(void *instance, unsigned knob, int value) {
  firmware_instance *const self = instance;
  const ifx_firmware_ops *const ops = self->ops;
  if (knob >= ops->effect->knobs || value < 0 ||
      !ifx_emu_send(self->emu, (unsigned)ops->effect->knob[knob].param, (unsigned)value)) {
    fprintf(stderr, "%s: send %u=%d failed\n", ops->effect->name, knob, value);
    return -1;
  }
  self->values[knob] = value;
  return 0;
}

int ifx_firmware_load(void *instance, const uint8_t *values) {
  firmware_instance *const self = instance;
  const ifx_firmware_ops *const ops = self->ops;
  if (!ifx_emu_start(self->emu, ops->config, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < ops->effect->knobs; knob++) {
    self->values[knob] = values[ops->effect->knob[knob].param];
  }
  return 0;
}

const char *ifx_firmware_eprom_path(const void *instance) {
  const firmware_instance *self = instance;
  return ifx_emu_eprom_path(self->emu);
}

int ifx_firmware_words(void *instance, int32_t *words) {
  firmware_instance *const self = instance;
  const ifx_firmware_ops *const ops = self->ops;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  unsigned at = 0;
  for (unsigned entry = 0; entry < ops->word_count; entry++) {
    const ifx_firmware_word *word = &ops->words[entry];
    switch (word->kind) {
    case IFX_FW_WORD: {
      const uint32_t raw = ifx_dsp_board_word(dsp, word->space, word->address);
      words[at++] = word->mask == 0xFFFFFFu ? ifx_s24(raw) : (int32_t)(raw & word->mask);
      break;
    }
    case IFX_FW_RANGE:
      for (unsigned i = 0; i < word->count; i++) {
        words[at++] = ifx_s24(ifx_dsp_board_word(dsp, word->space, (uint32_t)word->address + i));
      }
      break;
    case IFX_FW_STAGED:
      words[at++] = ifx_s24(ifx_dsp_board_word(dsp, DSP_SPACE_P,
                                               IFX_FIRMWARE_STAGED_TABLE +
                                                   ((unsigned)self->values[word->knob] & 0x3Fu)));
      break;
    }
  }
  return at == ops->effect->setup_words ? 0 : -1;
}

int ifx_firmware_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_instance *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}
