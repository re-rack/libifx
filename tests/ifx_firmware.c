#include "ifx_firmware.h"

#include "ifx_dsp_math.h"

#include <stdio.h>
#include <stdlib.h>

enum { MAX_KNOBS = 64 };

typedef struct {
  const ifx_firmware *firmware;
  ifx_emu *emu;
  int32_t values[MAX_KNOBS];
} firmware_instance;

void *ifx_firmware_open(const ifx_firmware *firmware) {
  const ifx_effect *const effect = firmware->effect;
  if (effect->knobs > MAX_KNOBS) {
    return NULL;
  }
  firmware_instance *self = calloc(1, sizeof *self);
  if (self == NULL) {
    return NULL;
  }
  self->firmware = firmware;
  uint8_t defaults[IFX_EMU_PARAMETER_COUNT] = {0};
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    defaults[effect->knob[knob].param] = (uint8_t)effect->knob[knob].def;
    self->values[knob] = effect->knob[knob].def;
  }
  self->emu = ifx_emu_create(NULL);
  if (self->emu == NULL || !ifx_emu_start(self->emu, firmware->config, defaults)) {
    ifx_firmware_close(self);
    return NULL;
  }
  return self;
}

void ifx_firmware_close(void *instance) {
  firmware_instance *const self = instance;
  if (self != NULL) {
    if (self->emu != NULL) {
      ifx_emu_destroy(self->emu);
    }
    free(self);
  }
}

int ifx_firmware_set_knob(void *instance, unsigned knob, int value) {
  firmware_instance *const self = instance;
  const ifx_effect *const effect = self->firmware->effect;
  if (knob >= effect->knobs || value < 0 ||
      !ifx_emu_send(self->emu, (unsigned)effect->knob[knob].param, (unsigned)value)) {
    fprintf(stderr, "%s: send %u=%d failed\n", effect->name, knob, value);
    return -1;
  }
  self->values[knob] = value;
  return 0;
}

int ifx_firmware_load(void *instance, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  firmware_instance *const self = instance;
  const ifx_effect *const effect = self->firmware->effect;
  if (!ifx_emu_start(self->emu, self->firmware->config, values)) {
    return -1;
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    self->values[knob] = values[effect->knob[knob].param];
  }
  return 0;
}

const ifx_emu *ifx_firmware_emu(const void *instance) {
  const firmware_instance *const self = instance;
  return self->emu;
}

int ifx_firmware_words(void *instance, int32_t *words) {
  firmware_instance *const self = instance;
  const ifx_firmware *const firmware = self->firmware;
  if (!ifx_emu_apply(self->emu)) {
    return -1;
  }
  const ifx_dsp_board *dsp = ifx_emu_dsp(self->emu);
  unsigned at = 0;
  for (unsigned entry = 0; entry < firmware->word_count; entry++) {
    const ifx_firmware_word *word = &firmware->words[entry];
    uint32_t address = word->address;
    if (word->source == IFX_FIRMWARE_STAGED) {
      address += (uint32_t)self->values[word->knob] & 0x3Fu; /* the handler's `& $3F` */
    }
    for (uint32_t i = 0; i < word->count; i++) {
      const uint32_t raw = ifx_dsp_board_word(dsp, word->space, address + i);
      words[at++] = word->mask == 0xFFFFFFu ? ifx_s24(raw) : (int32_t)(raw & word->mask);
    }
  }
  return at == firmware->effect->setup_words ? 0 : -1;
}

int ifx_firmware_frame(void *instance, int32_t in, int32_t *left, int32_t *right) {
  firmware_instance *const self = instance;
  ifx_emu_feed feed = {&in, 1u, false};
  return ifx_emu_audio(self->emu, &feed, left, right) ? 0 : -1;
}
