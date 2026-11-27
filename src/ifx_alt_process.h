/* What every de-popped variant's entry points share: the four entry points and
 * state_init, generated from the effect's own entry points and the variant's
 * hold function. For variant sources, not for hosts.
 *
 * A variant's state holds `exact`, the effect; `lane[2]` and `lane_config[2]`,
 * the frozen copies and the settings they keep (ifx_alt_core.h's sources 1 and
 * 2); `exact_config`, the config the effect ran its last block with; and
 * `alt`, the crossfade. The copies share the effect's gather/play buffer
 * position, so every source completes its blocks on the same frame. */
#ifndef IFX_ALT_PROCESS_H
#define IFX_ALT_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ifx_alt_core.h"
#include "ifx_base.h"
#include "ifx_convert.h"
#include "ifx_process.h"

/* Defines ifx_<name>_alt_state_init and ifx_<name>_alt_process_split, _f32,
 * _interleaved and _frame. `hold_fn` is called as
 *
 *   uint32_t hold_fn(const ifx_<name>_config *from, const ifx_<name>_config *to)
 *
 * for a turn from one config to another and returns the blocks after the
 * turn's block within which its steps reach the effect's output. */
#define IFX_ALT_PROCESS_DEFINE(name, hold_fn)                                                      \
  void ifx_##name##_alt_state_init(ifx_##name##_alt_state *state,                                  \
                                   const ifx_##name##_alt_config *config) {                        \
    ifx_##name##_state_init(&state->exact, config);                                                \
    state->exact_config = *config;                                                                 \
    ifx_alt_core_state_init(&state->alt);                                                          \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_alt_process_split(ifx_##name##_alt_state *state,                               \
                                      const ifx_##name##_alt_config *config, const int32_t *in,    \
                                      int32_t *out_l, int32_t *out_r, size_t frames) {             \
    while (frames != 0u) {                                                                         \
      /* Up to and with the frame that completes the sources' block. The position is always        \
       * below a block; the mask shows the analyzers so. */                                        \
      const unsigned pos = state->exact.block.pos & (IFX_BLOCK_FRAMES - 1u);                       \
      size_t count = IFX_BLOCK_FRAMES - pos;                                                       \
      if (count > frames) {                                                                        \
        count = frames;                                                                            \
      }                                                                                            \
      const int completes = pos + count == IFX_BLOCK_FRAMES;                                       \
      if (completes && memcmp(config, &state->exact_config, sizeof *config) != 0) {                \
        const unsigned freeze =                                                                    \
            ifx_alt_core_turn(&state->alt, hold_fn(&state->exact_config, config));                 \
        if (freeze != IFX_ALT_CORE_NONE) {                                                         \
          state->lane[freeze - 1u] = state->exact;                                                 \
          state->lane_config[freeze - 1u] = state->exact_config;                                   \
        }                                                                                          \
        state->exact_config = *config;                                                             \
      }                                                                                            \
      int32_t input[IFX_BLOCK_FRAMES];                                                             \
      int32_t left[IFX_ALT_CORE_SOURCES][IFX_BLOCK_FRAMES];                                        \
      int32_t right[IFX_ALT_CORE_SOURCES][IFX_BLOCK_FRAMES];                                       \
      const int32_t *source_left[IFX_ALT_CORE_SOURCES] = {left[0], NULL, NULL};                    \
      const int32_t *source_right[IFX_ALT_CORE_SOURCES] = {right[0], NULL, NULL};                  \
      /* The outputs may be the input buffer. */                                                   \
      memcpy(input, in, count * sizeof *in);                                                       \
      ifx_##name##_process_split(&state->exact, config, input, left[0], right[0], count);          \
      for (unsigned source = 1; source < IFX_ALT_CORE_SOURCES; source++) {                         \
        if (ifx_alt_core_runs(&state->alt, source)) {                                              \
          ifx_##name##_process_split(&state->lane[source - 1u], &state->lane_config[source - 1u],  \
                                     input, left[source], right[source], count);                   \
          source_left[source] = left[source];                                                      \
          source_right[source] = right[source];                                                    \
        }                                                                                          \
      }                                                                                            \
      ifx_alt_core_mix(&state->alt, source_left, source_right, out_l, out_r, count);               \
      if (completes) {                                                                             \
        ifx_alt_core_block(&state->alt);                                                           \
      }                                                                                            \
      in += count;                                                                                 \
      out_l += count;                                                                              \
      out_r += count;                                                                              \
      frames -= count;                                                                             \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_alt_process_f32(ifx_##name##_alt_state *state,                                 \
                                    const ifx_##name##_alt_config *config, const float *in,        \
                                    float *out_l, float *out_r, size_t frames) {                   \
    int32_t mono[IFX_PROCESS_F32_FRAMES];                                                          \
    int32_t left[IFX_PROCESS_F32_FRAMES];                                                          \
    int32_t right[IFX_PROCESS_F32_FRAMES];                                                         \
    for (size_t done = 0; done < frames; done += IFX_PROCESS_F32_FRAMES) {                         \
      size_t count = frames - done;                                                                \
      if (count > IFX_PROCESS_F32_FRAMES) {                                                        \
        count = IFX_PROCESS_F32_FRAMES;                                                            \
      }                                                                                            \
      ifx_f32_to_q23(mono, in + done, count);                                                      \
      ifx_##name##_alt_process_split(state, config, mono, left, right, count);                     \
      ifx_q23_to_f32(out_l + done, left, count);                                                   \
      ifx_q23_to_f32(out_r + done, right, count);                                                  \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_alt_process_interleaved(ifx_##name##_alt_state *state,                         \
                                            const ifx_##name##_alt_config *config,                 \
                                            const int32_t *in, int32_t *out, size_t frames) {      \
    int32_t left[IFX_PROCESS_F32_FRAMES];                                                          \
    int32_t right[IFX_PROCESS_F32_FRAMES];                                                         \
    for (size_t done = 0; done < frames; done += IFX_PROCESS_F32_FRAMES) {                         \
      size_t count = frames - done;                                                                \
      if (count > IFX_PROCESS_F32_FRAMES) {                                                        \
        count = IFX_PROCESS_F32_FRAMES;                                                            \
      }                                                                                            \
      ifx_##name##_alt_process_split(state, config, in + done, left, right, count);                \
      for (size_t i = 0; i < count; i++) {                                                         \
        out[2u * (done + i)] = left[i];                                                            \
        out[2u * (done + i) + 1u] = right[i];                                                      \
      }                                                                                            \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_alt_process_frame(ifx_##name##_alt_state *state,                               \
                                      const ifx_##name##_alt_config *config, int32_t in,           \
                                      int32_t out[2]) {                                            \
    ifx_##name##_alt_process_split(state, config, &in, &out[0], &out[1], 1u);                      \
  }

#endif /* IFX_ALT_PROCESS_H */
