#include "ifx_chorus8.h"

#include "ifx_analog_core.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"
#include "ifx_regen_core.h"

static const ifx_chorus_core_layout k_layout = {
    .voices = 8,
    .phase_step = 0x1000,
    .filter_write = 0x200C,
    .filter_read = 0x2000,
    .filter2_write = 0x2005,
    .filter_m = 0x1FFF,
    .filter_stride = 2,
    .six_tap = 1,
    .voice_bus = 0,
    .lfo_base = 0x1800,
    .clamp_filter2 = 1,
    .clamp_window = 0x200,
    .sweep_m = 0x1FFF,
    .extra_asr = 0,
};

static void chorus8_block(ifx_chorus8_state *state, const ifx_chorus8_config *config,
                          const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                          int32_t out_r[IFX_BLOCK_FRAMES]) {
  const int32_t flags = config->coef[IFX_CHORUS8_COEF_FLAGS];
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_CHORUS8_COEF_HUSH_THRESHOLD],
      config->coef[IFX_CHORUS8_COEF_HUSH_SLOPE], config->coef[IFX_CHORUS8_COEF_HUSH_RELEASE],
      (flags & IFX_MIXER_DESIGN_FLAG_HUSH) != 0);

  ifx_chorus_core_process(
      &state->chorus, &k_layout, state->memory, &config->coef[IFX_CHORUS8_COEF_VOICES],
      config->coef[IFX_CHORUS8_COEF_REG_LEFT], config->coef[IFX_CHORUS8_COEF_REG_RIGHT], hush);
  int32_t wet_l[IFX_BLOCK_FRAMES];
  int32_t wet_r[IFX_BLOCK_FRAMES];
  ifx_regen_core_drain(state->memory, &state->chorus.read_l, &state->chorus.read_r, wet_l, wet_r);

  ifx_mixer_core_process(wet_l, wet_r, hush, config->coef[IFX_CHORUS8_COEF_WET],
                         config->coef[IFX_CHORUS8_COEF_DIRECT_LEFT],
                         config->coef[IFX_CHORUS8_COEF_DIRECT_RIGHT],
                         (flags & IFX_MIXER_DESIGN_FLAG_DIRECT) != 0, out_l, out_r);
  ifx_analog_core_process(&state->analog, in, config->direct_latch[0], config->direct_latch[1],
                          config->output_gain, out_l, out_r);
}

void ifx_chorus8_state_init(ifx_chorus8_state *state, const ifx_chorus8_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_chorus_core_state_init(&state->chorus, &k_layout, config->coef[IFX_CHORUS8_COEF_REG_LEFT],
                             config->coef[IFX_CHORUS8_COEF_REG_RIGHT]);
  ifx_analog_core_state_init(&state->analog, config->output_gain);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(chorus8, chorus8_block)
