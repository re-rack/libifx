#include "ifx_chorus8.h"

#include "ifx_chorus_core.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"

static void chorus8_block(ifx_chorus8_state *state, const ifx_chorus8_config *config,
                          const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                          int32_t out_r[IFX_BLOCK_FRAMES]) {
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_CHORUS8_COEF_HUSH_THRESHOLD],
      config->coef[IFX_CHORUS8_COEF_HUSH_SLOPE], config->coef[IFX_CHORUS8_COEF_HUSH_RELEASE],
      (config->coef[IFX_CHORUS8_COEF_FLAGS] & IFX_MIXER_FLAG_HUSH) != 0);
  ifx_chorus_core_process(&state->chorus, &config->coef[IFX_CHORUS8_COEF_VOICES],
                          config->coef[IFX_CHORUS8_COEF_REG_LEFT],
                          config->coef[IFX_CHORUS8_COEF_REG_RIGHT], hush, state->wet_left,
                          state->wet_right);

  int32_t mixed_l[IFX_BLOCK_FRAMES];
  int32_t mixed_r[IFX_BLOCK_FRAMES];
  const int direct_on = (config->coef[IFX_CHORUS8_COEF_FLAGS] & IFX_MIXER_FLAG_DIRECT) != 0;
  ifx_mixer_core_process(state->wet_left, state->wet_right, hush,
                         config->coef[IFX_CHORUS8_COEF_WET],
                         config->coef[IFX_CHORUS8_COEF_DIRECT_LEFT],
                         config->coef[IFX_CHORUS8_COEF_DIRECT_RIGHT], direct_on, mixed_l, mixed_r);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = ifx_store24(ifx_word_acc(mixed_l[i]) - ifx_word_acc(IFX_MIXER_OFFSET));
    out_r[i] = ifx_store24(ifx_word_acc(mixed_r[i]) - ifx_word_acc(IFX_MIXER_OFFSET));
  }
}

enum { SETTLE_BLOCKS = 380 };

void ifx_chorus8_state_init(ifx_chorus8_state *state, const ifx_chorus8_config *config) {
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = 0;
    state->wet_right[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_chorus_core_state_init(&state->chorus, config->coef[IFX_CHORUS8_COEF_REG_LEFT],
                             config->coef[IFX_CHORUS8_COEF_REG_RIGHT]);
  /* The device wipes and settles for this many silent blocks before a preset
   * runs; the LFO phases and rotating pointers must start where it leaves them. */
  int32_t silence[IFX_BLOCK_FRAMES] = {0};
  int32_t wet_l[IFX_BLOCK_FRAMES];
  int32_t wet_r[IFX_BLOCK_FRAMES];
  for (unsigned block = 0; block < SETTLE_BLOCKS; block++) {
    ifx_chorus_core_process(&state->chorus, &config->coef[IFX_CHORUS8_COEF_VOICES],
                            config->coef[IFX_CHORUS8_COEF_REG_LEFT],
                            config->coef[IFX_CHORUS8_COEF_REG_RIGHT], silence, wet_l, wet_r);
  }
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(chorus8, chorus8_block)
