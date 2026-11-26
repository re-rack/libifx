/* Bit-exactness of src/ifx_convert.c against the conversion's definition.
 *
 *   test_convert [--expect PATH] [--exhaustive]
 *
 * The reference is written here from the definition, not shared with src/: scale
 * by 2^23 in double, NaN to 0, clamp, round half to even. The grids need no
 * reference at all: every Q23 value has to convert there and back unchanged and
 * every tie to round to the even neighbour. --expect fails a build that fell
 * back to another path; --exhaustive checks all 2^32 float patterns.
 */
#include "ifx_convert.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHUNK 1024u
#define GUARD_SAMPLES 4u
#define COUNT_MAX 70u
#define ARRAY_MAX (3u + COUNT_MAX + GUARD_SAMPLES)
#define RANDOM_CHUNKS 256u
#define EXHAUSTIVE_CHUNK 65536u
#define GUARD_WORD 0x5A5A5A5A
#define GUARD_FLOAT 42.0f

static uint32_t f32_bits(float x) {
  uint32_t bits;
  memcpy(&bits, &x, sizeof bits);
  return bits;
}

static float f32_from_bits(uint32_t bits) {
  float x;
  memcpy(&x, &bits, sizeof x);
  return x;
}

static int32_t ref_f32_to_q23(float x) {
  double v = (double)x * 8388608.0;
  if (v != v) {
    return 0;
  }
  if (v < -8388608.0) {
    v = -8388608.0;
  } else if (v > 8388607.0) {
    v = 8388607.0;
  }
  return (int32_t)rint(v);
}

/* The same definition without libm, for the exhaustive pass: at 1.5*2^52 the
 * double's ulp is 1.0, so the sum holds the rounded integer in its low 32 bits. */
static int32_t ref_f32_to_q23_wide(uint32_t bits) {
  double v = (double)f32_from_bits(bits) * 8388608.0;
  if (v != v) {
    return 0;
  }
  if (v < -8388608.0) {
    v = -8388608.0;
  } else if (v > 8388607.0) {
    v = 8388607.0;
  }
  double sum = v + 0x1.8p52;
  uint64_t raw;
  memcpy(&raw, &sum, sizeof raw);
  return (int32_t)(uint32_t)raw;
}

static float ref_q23_to_f32(int32_t word) {
  int32_t sample = (int32_t)(((uint32_t)word & 0xFFFFFFu) ^ 0x800000u) - 0x800000;
  return (float)sample * 0x1p-23f;
}

static double now_seconds(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static unsigned report(const char *what, unsigned differing, double start) {
  printf("     %-52s %u differing (%.1f ms)\n", what, differing, (now_seconds() - start) * 1e3);
  return differing;
}

/* Every low-24-bit pattern, alternating between a sign-extended word and one
 * whose bits above 23 are garbage. */
static unsigned check_q23_to_f32(void) {
  static int32_t in[CHUNK];
  static float got[CHUNK];
  static float want[CHUNK];
  unsigned differing = 0;
  double start = now_seconds();
  for (uint32_t base = 0; base < 0x1000000u; base += CHUNK) {
    for (uint32_t k = 0; k < CHUNK; k++) {
      uint32_t word = base + k;
      int32_t clean = (int32_t)word;
      if ((word & 0x800000u) != 0u) {
        clean -= 0x1000000;
      }
      in[k] = (k & 1u) != 0u ? clean : (int32_t)(word | 0x5A000000u);
      want[k] = ref_q23_to_f32(in[k]);
    }
    ifx_q23_to_f32(got, in, CHUNK);
    for (uint32_t k = 0; k < CHUNK; k++) {
      if (f32_bits(got[k]) != f32_bits(want[k])) {
        if (differing < 5u) {
          printf("     Q23 %06X -> %08X, want %08X\n", (unsigned)in[k] & 0xFFFFFFu,
                 f32_bits(got[k]), f32_bits(want[k]));
        }
        differing++;
      }
    }
  }
  return report("Q23 -> f32, all 2^24 patterns", differing, start);
}

/* k * 2^-23 has to convert back to k, and (k + 0.5) * 2^-23 to the even
 * neighbour, clamped where that neighbour leaves the Q23 range. Every tie is a
 * float: the largest, 8388607.5, needs 24 significand bits. */
static unsigned check_f32_grid(void) {
  static float in[CHUNK];
  static int32_t got[CHUNK];
  static int32_t want[CHUNK];
  unsigned differing = 0;
  double start = now_seconds();
  for (unsigned pass = 0; pass < 2u; pass++) {
    int32_t first = pass == 0u ? -0x800000 : -0x7FFFFF;
    const float bias = 0.5f * (float)pass;
    for (int32_t base = first; base <= 0x7FFFFF; base += (int32_t)CHUNK) {
      unsigned count = (unsigned)(0x800000 - base);
      if (count > CHUNK) {
        count = CHUNK;
      }
      for (unsigned k = 0; k < count; k++) {
        int32_t q = base + (int32_t)k;
        in[k] = ((float)q + bias) * 0x1p-23f;
        want[k] = q + (q & (int32_t)pass);
        if (want[k] > 0x7FFFFF) {
          want[k] = 0x7FFFFF;
        }
      }
      ifx_f32_to_q23(got, in, count);
      for (unsigned k = 0; k < count; k++) {
        if (got[k] != want[k]) {
          if (differing < 5u) {
            printf("     f32 %08X -> %d, want %d\n", f32_bits(in[k]), got[k], want[k]);
          }
          differing++;
        }
      }
    }
  }
  return report("f32 -> Q23, every Q23 value and every tie", differing, start);
}

static const struct {
  uint32_t bits;
  const char *name;
} k_specials[] = {
    {0x00000000u, "+0"},
    {0x80000000u, "-0"},
    {0x00000001u, "smallest denormal"},
    {0x007FFFFFu, "largest denormal"},
    {0x80000001u, "-smallest denormal"},
    {0x807FFFFFu, "-largest denormal"},
    {0x00800000u, "+FLT_MIN"},
    {0x80800000u, "-FLT_MIN"},
    {0x7F7FFFFFu, "+FLT_MAX"},
    {0xFF7FFFFFu, "-FLT_MAX"},
    {0x7F800000u, "+Inf"},
    {0xFF800000u, "-Inf"},
    {0x7FC00000u, "quiet NaN"},
    {0xFFC00000u, "-quiet NaN"},
    {0x7F800001u, "signalling NaN"},
    {0xFF800001u, "-signalling NaN"},
    {0x7FA5A5A5u, "NaN payload"},
    {0xFFA5A5A5u, "-NaN payload"},
    {0x3F800000u, "+1.0"},
    {0xBF800000u, "-1.0"},
    {0x3F7FFFFFu, "largest float below 1.0"},
    {0x3F000000u, "+0.5"},
    {0x4B000000u, "2^23"},
    {0xCB000000u, "-2^23"},
};

static unsigned check_value(const char *name, float x, unsigned differing) {
  int32_t want = ref_f32_to_q23(x);
  int32_t got = 0;
  ifx_f32_to_q23(&got, &x, 1);
  if (got != want) {
    if (differing < 5u) {
      printf("     %-22s f32 %08X -> %d, want %d\n", name, f32_bits(x), got, want);
    }
    differing++;
  }
  return differing;
}

/* The specials plus the float neighbours of both clamp edges, which is where
 * the rounding and the saturation meet. */
static unsigned check_special(void) {
  static const float k_edges[] = {1.0f, -1.0f, 0x1.FFFFFEp-1f, -0x1.FFFFFEp-1f};
  unsigned differing = 0;
  double start = now_seconds();
  for (size_t i = 0; i < sizeof k_specials / sizeof k_specials[0]; i++) {
    differing = check_value(k_specials[i].name, f32_from_bits(k_specials[i].bits), differing);
  }
  for (size_t edge = 0; edge < sizeof k_edges / sizeof k_edges[0]; edge++) {
    float v = k_edges[edge];
    for (unsigned step = 0; step < 4u; step++) {
      v = nextafterf(v, -INFINITY);
    }
    for (unsigned step = 0; step < 9u; step++) {
      differing = check_value("clamp edge neighbour", v, differing);
      v = nextafterf(v, INFINITY);
    }
  }
  return report("specials and clamp-edge neighbours", differing, start);
}

/* Deterministic raw bit patterns, which cover the exponent and mantissa
 * combinations the grids above never build. */
static unsigned check_random(void) {
  static float in[CHUNK];
  static int32_t got[CHUNK];
  unsigned differing = 0;
  uint32_t state = 0x6D2B79F5u;
  double start = now_seconds();
  for (unsigned chunk = 0; chunk < RANDOM_CHUNKS; chunk++) {
    for (uint32_t k = 0; k < CHUNK; k++) {
      state = state * 1664525u + 1013904223u;
      in[k] = f32_from_bits(state);
    }
    ifx_f32_to_q23(got, in, CHUNK);
    for (uint32_t k = 0; k < CHUNK; k++) {
      int32_t want = ref_f32_to_q23(in[k]);
      if (got[k] != want) {
        if (differing < 5u) {
          printf("     f32 %08X -> %d, want %d\n", f32_bits(in[k]), got[k], want);
        }
        differing++;
      }
    }
  }
  return report("pseudo-random bit patterns", differing, start);
}

/* Every count from 0 up, with the pointers moved off their natural alignment
 * and guard words around the destination: a tail that writes past dst + count
 * leaves a guard changed. */
static unsigned check_counts(void) {
  static float fsrc[ARRAY_MAX];
  static float fdst[ARRAY_MAX];
  static float fwant[ARRAY_MAX];
  static int32_t isrc[ARRAY_MAX];
  static int32_t idst[ARRAY_MAX];
  static int32_t iwant[ARRAY_MAX];
  unsigned differing = 0;
  uint32_t state = 0x9E3779B9u;
  double start = now_seconds();
  for (unsigned i = 0; i < ARRAY_MAX; i++) {
    state = state * 1664525u + 1013904223u;
    fsrc[i] = (float)((int32_t)(state >> 8) - 0x800000) * 0x1p-22f; /* -2 .. 2 */
    isrc[i] = (int32_t)((state << 3) | (state >> 29));
    fwant[i] = ref_q23_to_f32(isrc[i]);
    iwant[i] = ref_f32_to_q23(fsrc[i]);
  }
  for (unsigned offset = 1; offset <= 3u; offset++) {
    for (unsigned count = 0; count <= COUNT_MAX; count++) {
      for (unsigned i = 0; i < ARRAY_MAX; i++) {
        idst[i] = GUARD_WORD;
      }
      ifx_f32_to_q23(idst + offset, fsrc + offset, count);
      for (unsigned i = 0; i < ARRAY_MAX; i++) {
        int32_t want = i >= offset && i < offset + count ? iwant[i] : GUARD_WORD;
        if (idst[i] != want) {
          if (differing < 5u) {
            printf("     count %u offset %u: word %u is %d, want %d\n", count, offset, i, idst[i],
                   want);
          }
          differing++;
        }
      }
      for (unsigned i = 0; i < ARRAY_MAX; i++) {
        fdst[i] = GUARD_FLOAT;
      }
      ifx_q23_to_f32(fdst + offset, isrc + offset, count);
      for (unsigned i = 0; i < ARRAY_MAX; i++) {
        uint32_t want =
            i >= offset && i < offset + count ? f32_bits(fwant[i]) : f32_bits(GUARD_FLOAT);
        if (f32_bits(fdst[i]) != want) {
          if (differing < 5u) {
            printf("     count %u offset %u: sample %u is %08X, want %08X\n", count, offset, i,
                   f32_bits(fdst[i]), want);
          }
          differing++;
        }
      }
    }
  }
  return report("every count 0..70 and every pointer offset 1..3", differing, start);
}

/* All 2^32 float patterns. The wide reference is used because 4 billion libm
 * calls would cost far more than the conversions being tested. */
static unsigned check_exhaustive(void) {
  static float in[EXHAUSTIVE_CHUNK];
  static int32_t got[EXHAUSTIVE_CHUNK];
  static int32_t want[EXHAUSTIVE_CHUNK];
  unsigned differing = 0;
  double start = now_seconds();
  for (uint64_t base = 0; base < (UINT64_C(1) << 32); base += EXHAUSTIVE_CHUNK) {
    for (uint32_t k = 0; k < EXHAUSTIVE_CHUNK; k++) {
      uint32_t bits = (uint32_t)(base + k);
      in[k] = f32_from_bits(bits);
      want[k] = ref_f32_to_q23_wide(bits);
    }
    ifx_f32_to_q23(got, in, EXHAUSTIVE_CHUNK);
    for (uint32_t k = 0; k < EXHAUSTIVE_CHUNK; k++) {
      if (got[k] != want[k]) {
        if (differing < 5u) {
          printf("     f32 %08X -> %d, want %d\n", f32_bits(in[k]), got[k], want[k]);
        }
        differing++;
      }
    }
  }
  return report("all 2^32 float patterns", differing, start);
}

int main(int argc, char **argv) {
  const char *expect = NULL;
  int exhaustive = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
      expect = argv[++i];
    } else if (strcmp(argv[i], "--exhaustive") == 0) {
      exhaustive = 1;
    } else {
      fprintf(stderr, "usage: %s [--expect PATH] [--exhaustive]\n", argv[0]);
      return 2;
    }
  }
  unsigned differing = 0;
  if (expect != NULL && strcmp(ifx_convert_simd(), expect) != 0) {
    printf("     this build compiled the %s path, not %s\n", ifx_convert_simd(), expect);
    differing++;
  }
  differing += check_q23_to_f32();
  differing += check_f32_grid();
  differing += check_special();
  differing += check_random();
  differing += check_counts();
  if (exhaustive != 0) {
    differing += check_exhaustive();
  }
  printf("convert         %s  %s\n", differing == 0 ? "PASS" : "FAIL", ifx_convert_simd());
  return differing == 0 ? 0 : 1;
}
