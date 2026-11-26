#include "ifx_chorus_reverb.h"

#include "ifx_dsp_math.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"

static void chorus_reverb_block(ifx_chorus_reverb_state *state,
                                const ifx_chorus_reverb_config *config,
                                const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                                int32_t out_r[IFX_BLOCK_FRAMES]) {
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(&state->hush, front, hush, level,
                        config->coef[IFX_CHORUS_REVERB_COEF_HUSH_THRESHOLD],
                        config->coef[IFX_CHORUS_REVERB_COEF_HUSH_SLOPE],
                        config->coef[IFX_CHORUS_REVERB_COEF_HUSH_RELEASE],
                        (config->coef[IFX_CHORUS_REVERB_COEF_FLAGS] & IFX_MIXER_FLAG_HUSH) != 0);

  ifx_ducker_core_process(&state->ducker, level,
                          config->coef[IFX_CHORUS_REVERB_COEF_DUCK_THRESHOLD],
                          config->coef[IFX_CHORUS_REVERB_COEF_DUCK_ATTEN],
                          config->coef[IFX_CHORUS_REVERB_COEF_DUCK_RELEASE], 1);

  ifx_chorus_core_process(
      &state->chorus, state->memory, &config->coef[IFX_CHORUS_REVERB_COEF_VOICES],
      config->coef[IFX_CHORUS_REVERB_COEF_REG_LEFT], config->coef[IFX_CHORUS_REVERB_COEF_REG_RIGHT],
      hush, state->wet_left, state->wet_right);

  int32_t reverb_l[IFX_BLOCK_FRAMES];
  int32_t reverb_r[IFX_BLOCK_FRAMES];
  const int32_t flags = config->coef[IFX_CHORUS_REVERB_COEF_FLAGS];
  ifx_reverb_net_process(
      &state->reverb_net, state->memory, hush, state->chorus.read_l, state->chorus.read_r,
      config->coef[IFX_CHORUS_REVERB_COEF_PAN_LEFT], config->coef[IFX_CHORUS_REVERB_COEF_PAN_RIGHT],
      config->coef[IFX_CHORUS_REVERB_COEF_MIX_LEFT], config->coef[IFX_CHORUS_REVERB_COEF_MIX_RIGHT],
      state->ducker.gain, config->coef[IFX_CHORUS_REVERB_COEF_DUCKER] & 1,
      (flags & IFX_CHORUS_REVERB_FLAG_REV_INPUT) != 0, config->coef[IFX_CHORUS_REVERB_COEF_DAMP],
      config->coef[IFX_CHORUS_REVERB_COEF_FEEDBACK], reverb_l, reverb_r);

  int32_t chorus_level = config->coef[IFX_CHORUS_REVERB_COEF_CHORUS_LEVEL];
  int32_t delay_level = config->coef[IFX_CHORUS_REVERB_COEF_DELAY_LEVEL];
  int32_t reverb_level = config->coef[IFX_CHORUS_REVERB_COEF_REVERB_LEVEL];
  const int32_t ducker_mode = config->coef[IFX_CHORUS_REVERB_COEF_DUCKER];
  if ((ducker_mode & 1) != 0) {
    delay_level = ifx_store24(ifx_mpyr(delay_level, state->ducker.gain));
  }
  if ((ducker_mode & 2) != 0) {
    reverb_level = ifx_store24(ifx_mpyr(reverb_level, state->ducker.gain));
  }
  uint32_t read_l = state->chorus.read_l;
  uint32_t read_r = state->chorus.read_r;
  for (unsigned frame = 0; frame < IFX_BLOCK_FRAMES; frame++) {
    int64_t a = ifx_mpy(chorus_level, state->chorus.voice_bus[frame]);
    a = ifx_mac(a, delay_level, state->memory[read_l & 0xFFFFu]);
    a = ifx_macr(a, reverb_level, reverb_l[frame]);
    state->wet_left[frame] = ifx_store24(a);
    state->chorus.voice_bus[frame] = 0;
    state->memory[read_l & 0xFFFFu] = 0;
    read_l = ifx_agu_step(read_l, 1, 0x3FFFu);

    a = ifx_mpy(chorus_level, state->chorus.voice_bus[32u + frame]);
    a = ifx_mac(a, delay_level, state->memory[read_r & 0xFFFFu]);
    a = ifx_macr(a, reverb_level, reverb_r[frame]);
    state->wet_right[frame] = ifx_store24(a);
    state->chorus.voice_bus[32u + frame] = 0;
    state->memory[read_r & 0xFFFFu] = 0;
    read_r = ifx_agu_step(read_r, 1, 0x3FFFu);
  }
  state->chorus.read_l = read_l;
  state->chorus.read_r = read_r;

  int32_t mixed_l[IFX_BLOCK_FRAMES];
  int32_t mixed_r[IFX_BLOCK_FRAMES];
  const int direct_on = (flags & IFX_MIXER_FLAG_DIRECT) != 0;
  ifx_mixer_core_process(
      state->wet_left, state->wet_right, hush, config->coef[IFX_CHORUS_REVERB_COEF_WET],
      config->coef[IFX_CHORUS_REVERB_COEF_DIRECT_LEFT],
      config->coef[IFX_CHORUS_REVERB_COEF_DIRECT_RIGHT], direct_on, mixed_l, mixed_r);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = ifx_mixer_core_output(mixed_l[i]);
    out_r[i] = ifx_mixer_core_output(mixed_r[i]);
  }
}

void ifx_chorus_reverb_state_init(ifx_chorus_reverb_state *state,
                                  const ifx_chorus_reverb_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = 0;
    state->wet_right[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_chorus_core_state_init(&state->chorus, &ifx_chorus_core_geometry_cfg0,
                             config->coef[IFX_CHORUS_REVERB_COEF_REG_LEFT],
                             config->coef[IFX_CHORUS_REVERB_COEF_REG_RIGHT]);
  ifx_ducker_core_state_init(&state->ducker, config->coef[IFX_CHORUS_REVERB_COEF_DUCK_RELEASE], 1);
  ifx_reverb_net_state_init(&state->reverb_net);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(chorus_reverb, chorus_reverb_block)
