#include "ifx_pitch_reverb.h"

#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"

static const ifx_pitch_core_layout k_layout = {
    .voices = 2,
    .phase_bits = 12,
    .rate_shift = 4,
    .filter1_base = 0x1100,
    .filter1_m = 0x7F,
    .filter2_base = 0x2000,
    .filter2_m = 0x0FFF,
    .six_tap = 0,
    .grain_phase = 0x0800,
    .window_base = 0x1000,
    .line_l = 0x4000,
    .line_r = 0x8000,
    .delay_voice = 1,
    .voice_bus = 1,
};

static void pitch_reverb_block(ifx_pitch_reverb_state *state, const ifx_pitch_reverb_config *config,
                               const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                               int32_t out_r[IFX_BLOCK_FRAMES]) {
  const int32_t flags = config->coef[IFX_PITCH_REVERB_COEF_FLAGS];
  const int32_t ducker_mode = config->coef[IFX_PITCH_REVERB_COEF_DUCKER];
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_PITCH_REVERB_COEF_HUSH_THRESHOLD],
      config->coef[IFX_PITCH_REVERB_COEF_HUSH_SLOPE],
      config->coef[IFX_PITCH_REVERB_COEF_HUSH_RELEASE], (flags & IFX_MIXER_DESIGN_FLAG_HUSH) != 0);
  ifx_ducker_core_process(&state->ducker, level, config->coef[IFX_PITCH_REVERB_COEF_DUCK_THRESHOLD],
                          config->coef[IFX_PITCH_REVERB_COEF_DUCK_ATTEN],
                          config->coef[IFX_PITCH_REVERB_COEF_DUCK_RELEASE], 1);

  ifx_pitch_core_process(&state->pitch, &k_layout, state->memory,
                         &config->coef[IFX_PITCH_REVERB_COEF_VOICES],
                         config->coef[IFX_PITCH_REVERB_COEF_REG_LEFT],
                         config->coef[IFX_PITCH_REVERB_COEF_REG_RIGHT], hush);

  int32_t reverb_l[IFX_BLOCK_FRAMES];
  int32_t reverb_r[IFX_BLOCK_FRAMES];
  ifx_reverb_net_core_process(
      &state->reverb, state->memory, hush, state->pitch.read_l, state->pitch.read_r,
      config->coef[IFX_PITCH_REVERB_COEF_PAN_LEFT], config->coef[IFX_PITCH_REVERB_COEF_PAN_RIGHT],
      config->coef[IFX_PITCH_REVERB_COEF_MIX_LEFT], config->coef[IFX_PITCH_REVERB_COEF_MIX_RIGHT],
      state->ducker.gain, ducker_mode, (flags & IFX_PITCH_REVERB_FLAG_REV_INPUT) != 0,
      config->coef[IFX_PITCH_REVERB_COEF_DAMP], config->coef[IFX_PITCH_REVERB_COEF_FEEDBACK],
      reverb_l, reverb_r);

  int32_t wet_l[IFX_BLOCK_FRAMES];
  int32_t wet_r[IFX_BLOCK_FRAMES];
  ifx_reverb_net_core_mix(state->memory, state->pitch.voice_bus, &state->pitch.read_l,
                          &state->pitch.read_r, config->coef[IFX_PITCH_REVERB_COEF_PITCH_LEVEL],
                          config->coef[IFX_PITCH_REVERB_COEF_DELAY_LEVEL],
                          config->coef[IFX_PITCH_REVERB_COEF_REVERB_LEVEL], state->ducker.gain,
                          ducker_mode, reverb_l, reverb_r, wet_l, wet_r);

  ifx_mixer_core_process(wet_l, wet_r, hush, config->coef[IFX_PITCH_REVERB_COEF_WET],
                         config->coef[IFX_PITCH_REVERB_COEF_DIRECT_LEFT],
                         config->coef[IFX_PITCH_REVERB_COEF_DIRECT_RIGHT],
                         (flags & IFX_MIXER_DESIGN_FLAG_DIRECT) != 0, out_l, out_r);
}

void ifx_pitch_reverb_state_init(ifx_pitch_reverb_state *state,
                                 const ifx_pitch_reverb_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_ducker_core_state_init(&state->ducker, config->coef[IFX_PITCH_REVERB_COEF_DUCK_RELEASE], 1);
  ifx_pitch_core_state_init(&state->pitch, &k_layout, state->memory,
                            config->coef[IFX_PITCH_REVERB_COEF_REG_LEFT],
                            config->coef[IFX_PITCH_REVERB_COEF_REG_RIGHT]);
  ifx_reverb_net_core_state_init(&state->reverb);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(pitch_reverb, pitch_reverb_block)
