#include "ifx_pitch.h"

#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"

static const ifx_pitch_core_layout k_layout = {
    .voices = 4,
    .phase_bits = 13,
    .rate_shift = 5,
    .filter1_base = 0x1C00,
    .filter1_m = 0x7F,
    .filter2_base = 0x2000,
    .filter2_m = 0x1FFF,
    .dual_filter = 1,
    .grain_phase = 0x1000,
    .window_base = 0x1800,
    .line_l = 0x8000,
    .line_r = 0xC000,
    .line_m = 0x3FFF,
    .delay_voice = 0,
    .voice_bus = 0,
    .final_line_read = 1,
};

static void pitch_block(ifx_pitch_state *state, const ifx_pitch_config *config,
                        const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                        int32_t out_r[IFX_BLOCK_FRAMES]) {
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_PITCH_COEF_HUSH_THRESHOLD],
      config->coef[IFX_PITCH_COEF_HUSH_SLOPE], config->coef[IFX_PITCH_COEF_HUSH_RELEASE],
      (config->coef[IFX_PITCH_COEF_FLAGS] & IFX_MIXER_FLAG_HUSH) != 0);
  ifx_pitch_core_process(
      &state->pitch, &k_layout, state->memory, &config->coef[IFX_PITCH_COEF_VOICES],
      config->coef[IFX_PITCH_COEF_REG_LEFT], config->coef[IFX_PITCH_COEF_REG_RIGHT], hush,
      state->wet_left, state->wet_right);

  int32_t mixed_l[IFX_BLOCK_FRAMES];
  int32_t mixed_r[IFX_BLOCK_FRAMES];
  const int direct_on = (config->coef[IFX_PITCH_COEF_FLAGS] & IFX_MIXER_FLAG_DIRECT) != 0;
  ifx_mixer_core_process(state->wet_left, state->wet_right, hush, config->coef[IFX_PITCH_COEF_WET],
                         config->coef[IFX_PITCH_COEF_DIRECT_LEFT],
                         config->coef[IFX_PITCH_COEF_DIRECT_RIGHT], direct_on, mixed_l, mixed_r);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = ifx_mixer_core_output(mixed_l[i]);
    out_r[i] = ifx_mixer_core_output(mixed_r[i]);
  }
}

void ifx_pitch_state_init(ifx_pitch_state *state, const ifx_pitch_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = 0;
    state->wet_right[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_pitch_core_state_init(&state->pitch, &k_layout, state->memory,
                            config->coef[IFX_PITCH_COEF_REG_LEFT],
                            config->coef[IFX_PITCH_COEF_REG_RIGHT]);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(pitch, pitch_block)
