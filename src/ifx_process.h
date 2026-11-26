/* What every effect's process entry points share: the one-block gather/play
 * buffer and the four entry points generated from an effect's block function.
 * For effect sources, not for hosts. */
#ifndef IFX_PROCESS_H
#define IFX_PROCESS_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_base.h"
#include "ifx_convert.h"
#include "ifx_dsp_math.h"

/* Frames the float entry point converts at a time, through the stack. */
#define IFX_PROCESS_F32_FRAMES 256u

/* The state member the macro finds: every effect state embeds one named
 * `block`. The first 32 frames play the zero rest output while the first block
 * gathers. */
typedef struct {
  int32_t in[IFX_BLOCK_FRAMES];
  int32_t out_l[IFX_BLOCK_FRAMES];
  int32_t out_r[IFX_BLOCK_FRAMES];
  unsigned pos;
} ifx_process_block;

static inline void ifx_process_block_init(ifx_process_block *block) {
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    block->in[i] = 0;
    block->out_l[i] = 0;
    block->out_r[i] = 0;
  }
  block->pos = 0;
}

/* Defines ifx_<name>_process_split, _f32, _interleaved and _frame. `block` is
 * the effect's block function, called as
 *
 *   block(ifx_<name>_state *state, const ifx_<name>_config *config,
 *         const int32_t in[IFX_BLOCK_FRAMES],
 *         int32_t out_l[IFX_BLOCK_FRAMES], int32_t out_r[IFX_BLOCK_FRAMES])
 *
 * when the 32nd frame of a block arrives. Its outputs play during the next 32
 * frames, so output frame m is the block's output for input frame m - 32. */
#define IFX_PROCESS_DEFINE(name, block_fn)                                                         \
  void ifx_##name##_process_split(ifx_##name##_state *state, const ifx_##name##_config *config,    \
                                  const int32_t *in, int32_t *out_l, int32_t *out_r,               \
                                  size_t frames) {                                                 \
    for (size_t i = 0; i < frames; i++) {                                                          \
      const int32_t sample = ifx_s24((uint32_t)in[i]);                                             \
      ifx_process_block *buffer = &state->block;                                                   \
      const unsigned pos = buffer->pos;                                                            \
      buffer->in[pos] = sample;                                                                    \
      out_l[i] = buffer->out_l[pos];                                                               \
      out_r[i] = buffer->out_r[pos];                                                               \
      if (pos + 1u == IFX_BLOCK_FRAMES) {                                                          \
        block_fn(state, config, buffer->in, buffer->out_l, buffer->out_r);                         \
        buffer->pos = 0;                                                                           \
      } else {                                                                                     \
        buffer->pos = pos + 1u;                                                                    \
      }                                                                                            \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_process_f32(ifx_##name##_state *state, const ifx_##name##_config *config,      \
                                const float *in, float *out_l, float *out_r, size_t frames) {      \
    int32_t mono[IFX_PROCESS_F32_FRAMES];                                                          \
    int32_t left[IFX_PROCESS_F32_FRAMES];                                                          \
    int32_t right[IFX_PROCESS_F32_FRAMES];                                                         \
    for (size_t done = 0; done < frames; done += IFX_PROCESS_F32_FRAMES) {                         \
      size_t count = frames - done;                                                                \
      if (count > IFX_PROCESS_F32_FRAMES) {                                                        \
        count = IFX_PROCESS_F32_FRAMES;                                                            \
      }                                                                                            \
      /* The whole chunk converts before either output writes, so in place works. */               \
      ifx_f32_to_q23(mono, in + done, count);                                                      \
      ifx_##name##_process_split(state, config, mono, left, right, count);                         \
      ifx_q23_to_f32(out_l + done, left, count);                                                   \
      ifx_q23_to_f32(out_r + done, right, count);                                                  \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_process_interleaved(ifx_##name##_state *state,                                 \
                                        const ifx_##name##_config *config, const int32_t *in,      \
                                        int32_t *out, size_t frames) {                             \
    for (size_t i = 0; i < frames; i++) {                                                          \
      const int32_t sample = ifx_s24((uint32_t)in[i]);                                             \
      ifx_process_block *buffer = &state->block;                                                   \
      const unsigned pos = buffer->pos;                                                            \
      buffer->in[pos] = sample;                                                                    \
      out[2u * i] = buffer->out_l[pos];                                                            \
      out[2u * i + 1u] = buffer->out_r[pos];                                                       \
      if (pos + 1u == IFX_BLOCK_FRAMES) {                                                          \
        block_fn(state, config, buffer->in, buffer->out_l, buffer->out_r);                         \
        buffer->pos = 0;                                                                           \
      } else {                                                                                     \
        buffer->pos = pos + 1u;                                                                    \
      }                                                                                            \
    }                                                                                              \
  }                                                                                                \
                                                                                                   \
  void ifx_##name##_process_frame(ifx_##name##_state *state, const ifx_##name##_config *config,    \
                                  int32_t in, int32_t out[2]) {                                    \
    ifx_process_block *buffer = &state->block;                                                     \
    const unsigned pos = buffer->pos;                                                              \
    buffer->in[pos] = ifx_s24((uint32_t)in);                                                       \
    out[0] = buffer->out_l[pos];                                                                   \
    out[1] = buffer->out_r[pos];                                                                   \
    if (pos + 1u == IFX_BLOCK_FRAMES) {                                                            \
      block_fn(state, config, buffer->in, buffer->out_l, buffer->out_r);                           \
      buffer->pos = 0;                                                                             \
    } else {                                                                                       \
      buffer->pos = pos + 1u;                                                                      \
    }                                                                                              \
  }

#endif /* IFX_PROCESS_H */
