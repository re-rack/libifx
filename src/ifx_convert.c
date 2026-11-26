/* Float <-> Q23 conversion, one implementation per compile-time path. */
#include "ifx_convert.h"

#include <float.h>
#include <string.h>

enum { CONVERT_Q23_MAX = 0x7FFFFF, CONVERT_Q23_MIN = -0x800000 };

#if !defined(IFX_NO_SIMD) && defined(__AVX512F__)
#define CONVERT_SIMD_AVX512 1
#elif !defined(IFX_NO_SIMD) && defined(__AVX2__)
#define CONVERT_SIMD_AVX2 1
#elif !defined(IFX_NO_SIMD) && defined(__SSE2__)
#define CONVERT_SIMD_SSE2 1
#elif !defined(IFX_NO_SIMD) && defined(__aarch64__) && defined(__ARM_NEON)
#define CONVERT_SIMD_NEON 1
#elif !defined(IFX_NO_SIMD) && defined(__riscv_zve32f) && defined(__riscv_v_intrinsic) &&          \
    __riscv_v_intrinsic >= 1000000
#define CONVERT_SIMD_RVV 1
#endif

#if defined(CONVERT_SIMD_AVX512) || defined(CONVERT_SIMD_AVX2) || defined(CONVERT_SIMD_SSE2)
#include <immintrin.h>
#elif defined(CONVERT_SIMD_NEON)
#include <arm_neon.h>
#elif defined(CONVERT_SIMD_RVV)
#include <riscv_vector.h>
#endif

/* The AVX-512 and RVV paths convert their tails in the vector unit, so they need
 * neither helper. */
#if !defined(CONVERT_SIMD_AVX512) && !defined(CONVERT_SIMD_RVV)

static int32_t convert_scalar_f32_to_q23(float x) {
#if FLT_EVAL_METHOD == 0
  /* At 1.5*2^52 a double's ulp is 1.0: the sum rounds v to the nearest even
   * integer, which lands in the low 32 bits. */
  float v = x * 0x1p23f;
  if (v != v) {
    return 0;
  }
  if (v < (float)CONVERT_Q23_MIN) {
    v = (float)CONVERT_Q23_MIN;
  } else if (v > (float)CONVERT_Q23_MAX) {
    v = (float)CONVERT_Q23_MAX;
  }
  double sum = (double)v + 0x1.8p52;
  uint64_t bits;
  memcpy(&bits, &sum, sizeof bits);
  return (int32_t)(uint32_t)bits;
#else
  /* The same rounding in integers, for targets that evaluate float
   * expressions in wider precision: mantissa * 2^(exponent - 127) is the exact
   * value scaled by 2^23. */
  uint32_t bits;
  memcpy(&bits, &x, sizeof bits);
  unsigned exponent = (bits >> 23) & 0xFFu;
  uint64_t mantissa = bits & 0x7FFFFFu;
  int negative = (int)(bits >> 31);
  if (exponent == 0xFFu) {
    if (mantissa != 0u) {
      return 0;
    }
    return negative != 0 ? CONVERT_Q23_MIN : CONVERT_Q23_MAX;
  }
  if (exponent != 0u) {
    mantissa |= 0x800000u;
  }
  int shift = (int)exponent - 127;
  int64_t rounded = 0;
  if (shift >= 0) {
    rounded = shift > 23 ? (int64_t)CONVERT_Q23_MAX + 1 : (int64_t)(mantissa << (unsigned)shift);
  } else if ((unsigned)-shift <= 24u) {
    unsigned drop = (unsigned)-shift;
    rounded = (int64_t)(mantissa >> drop);
    uint64_t rest = mantissa & ((UINT64_C(1) << drop) - 1u);
    uint64_t half = UINT64_C(1) << (drop - 1u);
    if (rest > half || (rest == half && (rounded & 1) != 0)) {
      rounded += 1;
    }
  }
  if (negative != 0) {
    return rounded > (int64_t)-CONVERT_Q23_MIN ? CONVERT_Q23_MIN : (int32_t)-rounded;
  }
  return rounded > (int64_t)CONVERT_Q23_MAX ? CONVERT_Q23_MAX : (int32_t)rounded;
#endif
}

/* Bits 23..0 as a signed value: the kernel convention, so the upper bits of the
 * word are ignored. */
static float convert_scalar_q23_to_f32(int32_t word) {
  int32_t sample = (int32_t)(((uint32_t)word & 0xFFFFFFu) ^ 0x800000u) - 0x800000;
  return (float)sample * 0x1p-23f;
}

#endif /* !CONVERT_SIMD_AVX512 && !CONVERT_SIMD_RVV */

#if defined(CONVERT_SIMD_SSE2)

void ifx_f32_to_q23(int32_t *restrict dst, const float *restrict src, size_t count) {
  const __m128 scale = _mm_set1_ps(0x1p23f);
  const __m128 lo = _mm_set1_ps((float)CONVERT_Q23_MIN);
  const __m128 hi = _mm_set1_ps((float)CONVERT_Q23_MAX);
  size_t i = 0;
  for (; i + 4u <= count; i += 4u) {
    __m128 x = _mm_loadu_ps(src + i);
    __m128 v = _mm_and_ps(_mm_mul_ps(x, scale), _mm_cmpord_ps(x, x));
    v = _mm_min_ps(_mm_max_ps(v, lo), hi);
    _mm_storeu_si128((__m128i *)(void *)(dst + i), _mm_cvtps_epi32(v));
  }
  for (; i < count; i++) {
    dst[i] = convert_scalar_f32_to_q23(src[i]);
  }
}

void ifx_q23_to_f32(float *restrict dst, const int32_t *restrict src, size_t count) {
  const __m128 scale = _mm_set1_ps(0x1p-31f);
  size_t i = 0;
  for (; i + 4u <= count; i += 4u) {
    __m128i x = _mm_slli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(src + i)), 8);
    _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_cvtepi32_ps(x), scale));
  }
  for (; i < count; i++) {
    dst[i] = convert_scalar_q23_to_f32(src[i]);
  }
}

#elif defined(CONVERT_SIMD_AVX2)

void ifx_f32_to_q23(int32_t *restrict dst, const float *restrict src, size_t count) {
  const __m256 scale = _mm256_set1_ps(0x1p23f);
  const __m256 lo = _mm256_set1_ps((float)CONVERT_Q23_MIN);
  const __m256 hi = _mm256_set1_ps((float)CONVERT_Q23_MAX);
  size_t i = 0;
  for (; i + 8u <= count; i += 8u) {
    __m256 x = _mm256_loadu_ps(src + i);
    __m256 v = _mm256_and_ps(_mm256_mul_ps(x, scale), _mm256_cmp_ps(x, x, _CMP_ORD_Q));
    v = _mm256_min_ps(_mm256_max_ps(v, lo), hi);
    _mm256_storeu_si256((__m256i *)(void *)(dst + i), _mm256_cvtps_epi32(v));
  }
  for (; i < count; i++) {
    dst[i] = convert_scalar_f32_to_q23(src[i]);
  }
}

void ifx_q23_to_f32(float *restrict dst, const int32_t *restrict src, size_t count) {
  const __m256 scale = _mm256_set1_ps(0x1p-31f);
  size_t i = 0;
  for (; i + 8u <= count; i += 8u) {
    __m256i x = _mm256_slli_epi32(_mm256_loadu_si256((const __m256i *)(const void *)(src + i)), 8);
    _mm256_storeu_ps(dst + i, _mm256_mul_ps(_mm256_cvtepi32_ps(x), scale));
  }
  for (; i < count; i++) {
    dst[i] = convert_scalar_q23_to_f32(src[i]);
  }
}

#elif defined(CONVERT_SIMD_AVX512)

static __m512i convert_q23_words(__m512 x) {
  const __m512 scale = _mm512_set1_ps(0x1p23f);
  const __m512 lo = _mm512_set1_ps((float)CONVERT_Q23_MIN);
  const __m512 hi = _mm512_set1_ps((float)CONVERT_Q23_MAX);
  __m512 v = _mm512_maskz_mul_ps(_mm512_cmp_ps_mask(x, x, _CMP_ORD_Q), x, scale);
  v = _mm512_min_ps(_mm512_max_ps(v, lo), hi);
  return _mm512_cvt_roundps_epi32(v, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
}

void ifx_f32_to_q23(int32_t *restrict dst, const float *restrict src, size_t count) {
  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    _mm512_storeu_si512((void *)(dst + i), convert_q23_words(_mm512_loadu_ps(src + i)));
  }
  if (i < count) {
    __mmask16 tail = (__mmask16)((1u << (unsigned)(count - i)) - 1u);
    _mm512_mask_storeu_epi32(dst + i, tail,
                             convert_q23_words(_mm512_maskz_loadu_ps(tail, src + i)));
  }
}

void ifx_q23_to_f32(float *restrict dst, const int32_t *restrict src, size_t count) {
  const __m512 scale = _mm512_set1_ps(0x1p-31f);
  size_t i = 0;
  for (; i + 16u <= count; i += 16u) {
    __m512i x = _mm512_slli_epi32(_mm512_loadu_si512((const void *)(src + i)), 8);
    _mm512_storeu_ps(dst + i, _mm512_mul_ps(_mm512_cvtepi32_ps(x), scale));
  }
  if (i < count) {
    __mmask16 tail = (__mmask16)((1u << (unsigned)(count - i)) - 1u);
    __m512i x = _mm512_slli_epi32(_mm512_maskz_loadu_epi32(tail, src + i), 8);
    _mm512_mask_storeu_ps(dst + i, tail, _mm512_mul_ps(_mm512_cvtepi32_ps(x), scale));
  }
}

#elif defined(CONVERT_SIMD_NEON)

void ifx_f32_to_q23(int32_t *restrict dst, const float *restrict src, size_t count) {
  const int32x4_t lo = vdupq_n_s32(CONVERT_Q23_MIN);
  const int32x4_t hi = vdupq_n_s32(CONVERT_Q23_MAX);
  size_t i = 0;
  for (; i + 4u <= count; i += 4u) {
    int32x4_t q = vcvtnq_s32_f32(vmulq_n_f32(vld1q_f32(src + i), 0x1p23f));
    vst1q_s32(dst + i, vminq_s32(vmaxq_s32(q, lo), hi));
  }
  for (; i < count; i++) {
    dst[i] = convert_scalar_f32_to_q23(src[i]);
  }
}

void ifx_q23_to_f32(float *restrict dst, const int32_t *restrict src, size_t count) {
  size_t i = 0;
  for (; i + 4u <= count; i += 4u) {
    int32x4_t x = vshlq_n_s32(vld1q_s32(src + i), 8);
    vst1q_f32(dst + i, vcvtq_n_f32_s32(x, 31));
  }
  for (; i < count; i++) {
    dst[i] = convert_scalar_q23_to_f32(src[i]);
  }
}

#elif defined(CONVERT_SIMD_RVV)

/* vfcvt.x.f turns NaN into INT32_MAX, so NaN lanes are zeroed after the clamp.
 * The scaling multiplies are exact, so only the conversion's own rounding mode
 * matters, and it is fixed to nearest even. */
void ifx_f32_to_q23(int32_t *restrict dst, const float *restrict src, size_t count) {
  while (count > 0u) {
    const size_t vl = __riscv_vsetvl_e32m8(count);
    const vfloat32m8_t x = __riscv_vle32_v_f32m8(src, vl);
    vint32m8_t q =
        __riscv_vfcvt_x_f_v_i32m8_rm(__riscv_vfmul_vf_f32m8(x, 0x1p23f, vl), __RISCV_FRM_RNE, vl);
    q = __riscv_vmin_vx_i32m8(__riscv_vmax_vx_i32m8(q, CONVERT_Q23_MIN, vl), CONVERT_Q23_MAX, vl);
    q = __riscv_vmerge_vxm_i32m8(q, 0, __riscv_vmfne_vv_f32m8_b4(x, x, vl), vl);
    __riscv_vse32_v_i32m8(dst, q, vl);
    src += vl;
    dst += vl;
    count -= vl;
  }
}

void ifx_q23_to_f32(float *restrict dst, const int32_t *restrict src, size_t count) {
  while (count > 0u) {
    const size_t vl = __riscv_vsetvl_e32m8(count);
    const vint32m8_t x = __riscv_vsll_vx_i32m8(__riscv_vle32_v_i32m8(src, vl), 8u, vl);
    __riscv_vse32_v_f32m8(
        dst, __riscv_vfmul_vf_f32m8(__riscv_vfcvt_f_x_v_f32m8(x, vl), 0x1p-31f, vl), vl);
    src += vl;
    dst += vl;
    count -= vl;
  }
}

#else

void ifx_f32_to_q23(int32_t *restrict dst, const float *restrict src, size_t count) {
  for (size_t i = 0; i < count; i++) {
    dst[i] = convert_scalar_f32_to_q23(src[i]);
  }
}

void ifx_q23_to_f32(float *restrict dst, const int32_t *restrict src, size_t count) {
  for (size_t i = 0; i < count; i++) {
    dst[i] = convert_scalar_q23_to_f32(src[i]);
  }
}

#endif

const char *ifx_convert_simd(void) {
#if defined(CONVERT_SIMD_AVX512)
  return "AVX-512";
#elif defined(CONVERT_SIMD_AVX2)
  return "AVX2";
#elif defined(CONVERT_SIMD_SSE2)
  return "SSE2";
#elif defined(CONVERT_SIMD_NEON)
  return "NEON";
#elif defined(CONVERT_SIMD_RVV)
  return "RVV";
#else
  return "scalar";
#endif
}
