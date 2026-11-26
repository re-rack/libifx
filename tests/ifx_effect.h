/* One effect described once: its knob table and the native adapter the checker
 * drives. The recorder reads the same table through the firmware, so the two
 * sides cannot drift. */
#ifndef IFX_EFFECT_H
#define IFX_EFFECT_H

#include "ifx.h"
#include "ifx_effects.h"
#include "ifx_process.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define IFX_EFFECT_RATE 39000u
#define IFX_EFFECT_SEGMENT 4096u
/* Frames a delay-time knob's maximum segment holds: the 2-Tap line's 1,500 ms. */
#define IFX_EFFECT_HOLD 60000u
#define IFX_EFFECT_SETTLE 8192u

typedef struct {
  const char *name;    /* as the firmware's name table prints it */
  int min;
  int max;
  int def;
  uint32_t max_frames; /* frames its max segment holds; 0 for IFX_EFFECT_SEGMENT */
  int active;          /* the value the active walk uses */
  int mode;            /* 1 for a mode knob: every value gets its own active walk */
} ifx_effect_knob;

typedef struct {
  const char *name; /* fixture stem */
  unsigned knobs;
  const ifx_effect_knob *knob;
  uint32_t setup_words; /* coefficient words per setup record */
  int (*setup)(const int32_t *values, int32_t *words);
  int (*rejects)(void);
  void *(*open)(const int32_t *values);
  int (*set_knob)(void *instance, unsigned knob, int value);
  void (*split)(void *instance, const int32_t *in, int32_t *out_left, int32_t *out_right,
                size_t frames);
  void (*f32)(void *instance, const float *in, float *out_left, float *out_right, size_t frames);
  void (*interleaved)(void *instance, const int32_t *in, int32_t *out, size_t frames);
  void (*frame)(void *instance, int32_t in, int32_t out[2]);
  void (*close)(void *instance);
} ifx_effect;

#define IFX_EFFECT_POISON 0xA5

#define IFX_EFFECT_DEFINE(fixture, module, knob_table, raw, words, word_count)                     \
  typedef struct {                                                                                 \
    ifx_context context;                                                                           \
    ifx_##module##_config config;                                                                  \
    ifx_##module##_state state;                                                                    \
    int32_t values[sizeof(knob_table) / sizeof(knob_table)[0]];                                    \
  } fixture##_native;                                                                              \
                                                                                                   \
  static int fixture##_config(const int32_t *values, const ifx_context *context,                   \
                              ifx_##module##_config *config) {                                     \
    ifx_##module##_setup_raw raw_values;                                                           \
    raw(values, &raw_values);                                                                      \
    return ifx_##module##_setup_config(context, &raw_values, config);                              \
  }                                                                                                \
                                                                                                   \
  static int fixture##_setup(const int32_t *values, int32_t *setup_words) {                        \
    ifx_context context;                                                                           \
    ifx_##module##_config config;                                                                  \
    memset(&config, IFX_EFFECT_POISON, sizeof config);                                             \
    if (ifx_context_init(&context, IFX_EFFECT_RATE) != 0 ||                                        \
        fixture##_config(values, &context, &config) != 0) {                                        \
      return -1;                                                                                   \
    }                                                                                              \
    words(&config, setup_words);                                                                   \
    return 0;                                                                                      \
  }                                                                                                \
                                                                                                   \
  static int fixture##_rejects(void) {                                                             \
    int32_t values[sizeof(knob_table) / sizeof(knob_table)[0]] = {0};                              \
    for (size_t knob = 0; knob < sizeof(knob_table) / sizeof(knob_table)[0]; knob++) {             \
      values[knob] = (knob_table)[knob].def;                                                       \
    }                                                                                              \
    ifx_context context;                                                                           \
    const ifx_context no_rate = {0};                                                               \
    ifx_##module##_setup_raw raw_values;                                                           \
    ifx_##module##_config config;                                                                  \
    raw(values, &raw_values);                                                                      \
    if (ifx_context_init(&context, IFX_EFFECT_RATE) != 0) {                                        \
      return -1;                                                                                   \
    }                                                                                              \
    return ifx_##module##_setup_config(NULL, &raw_values, &config) == -1 &&                        \
                   ifx_##module##_setup_config(&no_rate, &raw_values, &config) == -1 &&            \
                   ifx_##module##_setup_config(&context, NULL, &config) == -1 &&                   \
                   ifx_##module##_setup_config(&context, &raw_values, NULL) == -1                  \
               ? 0                                                                                 \
               : -1;                                                                               \
  }                                                                                                \
                                                                                                   \
  static void *fixture##_open(const int32_t *values) {                                             \
    fixture##_native *self = malloc(sizeof *self);                                                 \
    if (self == NULL) {                                                                            \
      return NULL;                                                                                 \
    }                                                                                              \
    memset(self, IFX_EFFECT_POISON, sizeof *self);                                                 \
    memcpy(self->values, values, sizeof self->values);                                             \
    if (ifx_context_init(&self->context, IFX_EFFECT_RATE) != 0 ||                                  \
        fixture##_config(self->values, &self->context, &self->config) != 0) {                      \
      free(self);                                                                                  \
      return NULL;                                                                                 \
    }                                                                                              \
    ifx_##module##_state_init(&self->state, &self->config);                                        \
    return self;                                                                                   \
  }                                                                                                \
                                                                                                   \
  static int fixture##_set_knob(void *instance, unsigned knob, int value) {                        \
    fixture##_native *self = instance;                                                             \
    const int32_t previous = self->values[knob];                                                   \
    ifx_##module##_config config;                                                                  \
    self->values[knob] = (int32_t)value;                                                           \
    memset(&config, IFX_EFFECT_POISON, sizeof config);                                             \
    if (fixture##_config(self->values, &self->context, &config) != 0) {                            \
      self->values[knob] = previous;                                                               \
      return -1;                                                                                   \
    }                                                                                              \
    self->config = config;                                                                         \
    return 0;                                                                                      \
  }                                                                                                \
                                                                                                   \
  static void fixture##_split(void *instance, const int32_t *in, int32_t *out_left,                \
                              int32_t *out_right, size_t frames) {                                 \
    fixture##_native *self = instance;                                                             \
    ifx_##module##_process_split(&self->state, &self->config, in, out_left, out_right, frames);    \
  }                                                                                                \
                                                                                                   \
  static void fixture##_f32(void *instance, const float *in, float *out_left, float *out_right,    \
                            size_t frames) {                                                       \
    fixture##_native *self = instance;                                                             \
    ifx_##module##_process_f32(&self->state, &self->config, in, out_left, out_right, frames);      \
  }                                                                                                \
                                                                                                   \
  static void fixture##_interleaved(void *instance, const int32_t *in, int32_t *out,               \
                                    size_t frames) {                                               \
    fixture##_native *self = instance;                                                             \
    ifx_##module##_process_interleaved(&self->state, &self->config, in, out, frames);              \
  }                                                                                                \
                                                                                                   \
  static void fixture##_frame(void *instance, int32_t in, int32_t out[2]) {                        \
    fixture##_native *self = instance;                                                             \
    ifx_##module##_process_frame(&self->state, &self->config, in, out);                            \
  }                                                                                                \
                                                                                                   \
  static void fixture##_close(void *instance) { free(instance); }                                  \
                                                                                                   \
  const ifx_effect ifx_effect_##fixture = {                                                        \
      .name = #fixture,                                                                            \
      .knobs = (unsigned)(sizeof(knob_table) / sizeof(knob_table)[0]),                             \
      .knob = (knob_table),                                                                        \
      .setup_words = (word_count),                                                                 \
      .setup = fixture##_setup,                                                                    \
      .rejects = fixture##_rejects,                                                                \
      .open = fixture##_open,                                                                      \
      .set_knob = fixture##_set_knob,                                                              \
      .split = fixture##_split,                                                                    \
      .f32 = fixture##_f32,                                                                        \
      .interleaved = fixture##_interleaved,                                                        \
      .frame = fixture##_frame,                                                                    \
      .close = fixture##_close,                                                                    \
  };

#define IFX_EFFECT_FAST 64u
#define IFX_EFFECT_FAST_TURNS 10u
#define IFX_ALL (-1)

typedef struct {
  int knob;
  int value;
  uint32_t frames;
} ifx_segment;

#define IFX_EFFECT_SEGMENTS(knobs)                                                                 \
  (5u + (10u + 2u * IFX_EFFECT_FAST_TURNS) * (knobs) + 8u * (2u * (knobs) + 1u))

static inline int ifx_knob_clamp(const ifx_effect_knob *knob, int value) {
  return value < knob->min ? knob->min : value > knob->max ? knob->max : value;
}

static inline int ifx_knob_at(const ifx_effect_knob *knob, unsigned num, unsigned den) {
  const unsigned span = (unsigned)(knob->max - knob->min);
  return knob->min + (int)((span * num + den / 2u) / den);
}

static inline unsigned ifx_effect_segments(const ifx_effect *effect, ifx_segment *segment,
                                           unsigned cap) {
  if (cap < IFX_EFFECT_SEGMENTS(effect->knobs)) {
    return 0;
  }
  unsigned n = 0;
  segment[n++] = (ifx_segment){IFX_ALL, 0, IFX_EFFECT_SEGMENT};
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const ifx_effect_knob *record = &effect->knob[knob];
    uint32_t longest = record->max_frames != 0u ? record->max_frames : IFX_EFFECT_SEGMENT;
    segment[n++] = (ifx_segment){(int)knob, record->min, IFX_EFFECT_SEGMENT};
    segment[n++] = (ifx_segment){(int)knob, ifx_knob_at(record, 1u, 3u), IFX_EFFECT_SEGMENT};
    segment[n++] = (ifx_segment){(int)knob, ifx_knob_at(record, 2u, 3u), IFX_EFFECT_SEGMENT};
    segment[n++] = (ifx_segment){(int)knob, record->max, longest};
    segment[n++] = (ifx_segment){(int)knob, record->def, IFX_EFFECT_SEGMENT};
  }
  segment[n++] = (ifx_segment){IFX_ALL, 0, IFX_EFFECT_SEGMENT};
  segment[n++] = (ifx_segment){IFX_ALL, 0, IFX_EFFECT_SEGMENT};
  for (unsigned step = effect->knobs; step > 0u; step--) {
    const unsigned knob = step - 1u;
    const ifx_effect_knob *record = &effect->knob[knob];
    segment[n++] = (ifx_segment){(int)knob, record->active, IFX_EFFECT_SEGMENT};
  }
  for (unsigned mode_knob = 0; mode_knob < effect->knobs; mode_knob++) {
    const ifx_effect_knob *mode_record = &effect->knob[mode_knob];
    if (mode_record->mode == 0) {
      continue;
    }
    for (int mode_value = mode_record->min; mode_value <= mode_record->max; mode_value++) {
      segment[n++] = (ifx_segment){(int)mode_knob, mode_value, IFX_EFFECT_SETTLE};
      for (unsigned knob = 0; knob < effect->knobs; knob++) {
        if (knob == mode_knob) {
          continue;
        }
        segment[n++] = (ifx_segment){(int)knob, effect->knob[knob].def, IFX_EFFECT_SEGMENT};
      }
      for (unsigned step = effect->knobs; step > 0u; step--) {
        const unsigned knob = step - 1u;
        if (knob == mode_knob) {
          continue;
        }
        segment[n++] = (ifx_segment){(int)knob, effect->knob[knob].active, IFX_EFFECT_SEGMENT};
      }
    }
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const ifx_effect_knob *record = &effect->knob[knob];
    uint32_t longest = record->max_frames != 0u ? record->max_frames : IFX_EFFECT_SEGMENT;
    segment[n++] = (ifx_segment){(int)knob, ifx_knob_at(record, 1u, 3u), IFX_EFFECT_SEGMENT};
    segment[n++] = (ifx_segment){(int)knob, record->max, longest};
    segment[n++] = (ifx_segment){(int)knob, record->active, IFX_EFFECT_SEGMENT};
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const ifx_effect_knob *record = &effect->knob[knob];
    const int active = record->active;
    const int fast[IFX_EFFECT_FAST_TURNS] = {active + 1,
                                             active + 2,
                                             active + 3,
                                             ifx_knob_at(record, 1u, 3u),
                                             ifx_knob_at(record, 2u, 3u),
                                             record->max,
                                             record->min,
                                             record->min + 1,
                                             record->min + 2,
                                             active};
    for (unsigned turn = 0; turn < IFX_EFFECT_FAST_TURNS; turn++) {
      segment[n++] = (ifx_segment){(int)knob, ifx_knob_clamp(record, fast[turn]), IFX_EFFECT_FAST};
    }
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const ifx_effect_knob *record = &effect->knob[knob];
    segment[n++] = (ifx_segment){(int)knob, record->def, IFX_EFFECT_FAST};
  }
  segment[n++] = (ifx_segment){IFX_ALL, 0, IFX_EFFECT_SEGMENT};
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    const ifx_effect_knob *record = &effect->knob[knob];
    const int def = record->def;
    const int fast[IFX_EFFECT_FAST_TURNS] = {def + 1,
                                             def + 2,
                                             def + 3,
                                             ifx_knob_at(record, 1u, 3u),
                                             ifx_knob_at(record, 2u, 3u),
                                             record->max,
                                             record->min,
                                             record->min + 1,
                                             record->min + 2,
                                             def};
    for (unsigned turn = 0; turn < IFX_EFFECT_FAST_TURNS; turn++) {
      segment[n++] = (ifx_segment){(int)knob, ifx_knob_clamp(record, fast[turn]), IFX_EFFECT_FAST};
    }
  }
  segment[n++] = (ifx_segment){IFX_ALL, 0, IFX_EFFECT_SEGMENT};
  for (unsigned i = 0; i < n; i++) {
    segment[i].frames += i % 4u;
  }
  return n;
}

static inline void ifx_effect_defaults(const ifx_effect *effect, int32_t *values) {
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    values[knob] = effect->knob[knob].def;
  }
}

static inline void ifx_effect_values(const ifx_effect *effect, const ifx_segment *segment,
                                     int32_t *values) {
  if (segment->knob == IFX_ALL) {
    ifx_effect_defaults(effect, values);
  } else {
    values[segment->knob] = segment->value;
  }
}

#define IFX_EFFECT_DECLARE(name) extern const ifx_effect ifx_effect_##name;
IFX_EFFECTS(IFX_EFFECT_DECLARE)
#undef IFX_EFFECT_DECLARE

#endif /* IFX_EFFECT_H */
