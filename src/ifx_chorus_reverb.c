#include "ifx_chorus_reverb.h"

#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"

static const ifx_chorus_core_layout k_layout = {
    .voices = 4,
    .phase_step = 0x800,
    .filter_write = 0x2000,
    .filter_read = 0,
    .filter2_write = 0,
    .filter_m = 0x0FFF,
    .filter_stride = 1,
    .six_tap = 0,
    .voice_bus = 1,
    .lfo_base = 0x1000,
    .clamp_filter2 = 0,
    .clamp_window = 0x100,
    .sweep_m = 0x0FFF,
    .extra_asr = 1,
};

static void chorus_reverb_block(ifx_chorus_reverb_state *state,
                                const ifx_chorus_reverb_config *config,
                                const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                                int32_t out_r[IFX_BLOCK_FRAMES]) {
  const int32_t flags = config->coef[IFX_CHORUS_REVERB_COEF_FLAGS];
  const int32_t ducker_mode = config->coef[IFX_CHORUS_REVERB_COEF_DUCKER];
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_CHORUS_REVERB_COEF_HUSH_THRESHOLD],
      config->coef[IFX_CHORUS_REVERB_COEF_HUSH_SLOPE],
      config->coef[IFX_CHORUS_REVERB_COEF_HUSH_RELEASE], (flags & IFX_MIXER_DESIGN_FLAG_HUSH) != 0);
  ifx_ducker_core_process(&state->ducker, level,
                          config->coef[IFX_CHORUS_REVERB_COEF_DUCK_THRESHOLD],
                          config->coef[IFX_CHORUS_REVERB_COEF_DUCK_ATTEN],
                          config->coef[IFX_CHORUS_REVERB_COEF_DUCK_RELEASE], 1);

  ifx_chorus_core_process(&state->chorus, &k_layout, state->memory,
                          &config->coef[IFX_CHORUS_REVERB_COEF_VOICES],
                          config->coef[IFX_CHORUS_REVERB_COEF_REG_LEFT],
                          config->coef[IFX_CHORUS_REVERB_COEF_REG_RIGHT], hush);

  int32_t reverb_l[IFX_BLOCK_FRAMES];
  int32_t reverb_r[IFX_BLOCK_FRAMES];
  ifx_reverb_net_core_process(
      &state->reverb, state->memory, hush, state->chorus.read_l, state->chorus.read_r,
      config->coef[IFX_CHORUS_REVERB_COEF_PAN_LEFT], config->coef[IFX_CHORUS_REVERB_COEF_PAN_RIGHT],
      config->coef[IFX_CHORUS_REVERB_COEF_MIX_LEFT], config->coef[IFX_CHORUS_REVERB_COEF_MIX_RIGHT],
      state->ducker.gain, ducker_mode, (flags & IFX_CHORUS_REVERB_FLAG_REV_INPUT) != 0,
      config->coef[IFX_CHORUS_REVERB_COEF_DAMP], config->coef[IFX_CHORUS_REVERB_COEF_FEEDBACK],
      reverb_l, reverb_r);

  int32_t wet_l[IFX_BLOCK_FRAMES];
  int32_t wet_r[IFX_BLOCK_FRAMES];
  ifx_reverb_net_core_mix(state->memory, state->chorus.voice_bus, &state->chorus.read_l,
                          &state->chorus.read_r, config->coef[IFX_CHORUS_REVERB_COEF_CHORUS_LEVEL],
                          config->coef[IFX_CHORUS_REVERB_COEF_DELAY_LEVEL],
                          config->coef[IFX_CHORUS_REVERB_COEF_REVERB_LEVEL], state->ducker.gain,
                          ducker_mode, reverb_l, reverb_r, wet_l, wet_r);

  ifx_mixer_core_process(wet_l, wet_r, hush, config->coef[IFX_CHORUS_REVERB_COEF_WET],
                         config->coef[IFX_CHORUS_REVERB_COEF_DIRECT_LEFT],
                         config->coef[IFX_CHORUS_REVERB_COEF_DIRECT_RIGHT],
                         (flags & IFX_MIXER_DESIGN_FLAG_DIRECT) != 0, out_l, out_r);
}

void ifx_chorus_reverb_state_init(ifx_chorus_reverb_state *state,
                                  const ifx_chorus_reverb_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_ducker_core_state_init(&state->ducker, config->coef[IFX_CHORUS_REVERB_COEF_DUCK_RELEASE], 1);
  ifx_chorus_core_state_init(&state->chorus, &k_layout,
                             config->coef[IFX_CHORUS_REVERB_COEF_REG_LEFT],
                             config->coef[IFX_CHORUS_REVERB_COEF_REG_RIGHT]);
  ifx_reverb_net_core_state_init(&state->reverb);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(chorus_reverb, chorus_reverb_block)
