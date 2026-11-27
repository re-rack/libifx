/* The eased ramp the analog stages and the de-popped variants move along: a
 * smoothstep, whose slope is zero at both ends, so a move adds neither a step
 * nor a kink to the signal. */
#ifndef IFX_FADE_H
#define IFX_FADE_H

#include <stdint.h>

#define IFX_FADE_SHIFT 10u
#define IFX_FADE_FRAMES (1u << IFX_FADE_SHIFT)

/* A whole share: IFX_FADE_FRAMES cubed. */
#define IFX_FADE_ONE ((int64_t)1 << (3u * IFX_FADE_SHIFT))

/* The share of the way at frame `pos` of IFX_FADE_FRAMES: 0 at 0, IFX_FADE_ONE
 * at IFX_FADE_FRAMES. */
static inline int64_t ifx_fade_share(uint32_t pos) {
  const int64_t x = pos;
  return x * x * (3 * (int64_t)IFX_FADE_FRAMES - 2 * x);
}

/* `share` of the way from `from` to `to`, rounded; a whole share gives `to`. */
static inline int32_t ifx_fade_blend(int32_t from, int32_t to, int64_t share) {
  return from +
         (int32_t)((((int64_t)to - from) * share + (IFX_FADE_ONE >> 1)) >> (3u * IFX_FADE_SHIFT));
}

#endif /* IFX_FADE_H */
