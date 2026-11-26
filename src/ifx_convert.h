/* Float <-> Q23 sample conversion: plugins exchange 32-bit float audio, while
 * the bit-exact kernels work on Q23, a signed 24-bit sample held in an int32.
 *
 * f32 -> Q23 scales by 2^23, turns NaN into 0, rounds to nearest even and
 * saturates to [-0x800000, 0x7FFFFF]: +-Inf and anything at or beyond +-1.0
 * saturate, -1.0 gives -0x800000, denormals give 0. Q23 -> f32 reads bits 23..0
 * as a signed value and scales by 2^-23, which is exact.
 *
 * The path is chosen when the translation unit is compiled, in this order:
 * __AVX512F__, __AVX2__, __SSE2__ (present on every x86-64 target), AArch64
 * NEON, the RISC-V vector extension (V or Zve32f, intrinsics 1.0), otherwise
 * scalar. Defining IFX_NO_SIMD forces the scalar path. There is no runtime
 * dispatch and no global state.
 *
 * SSE2, AVX2 and the scalar path round in the current FP rounding mode; the
 * default round-to-nearest is assumed. FTZ and DAZ cannot change any result.
 */
#ifndef IFX_CONVERT_H
#define IFX_CONVERT_H

#include <stddef.h>
#include <stdint.h>

/* `dst` and `src` must not overlap. */
void ifx_f32_to_q23(int32_t *dst, const float *src, size_t count);
void ifx_q23_to_f32(float *dst, const int32_t *src, size_t count);

/* The path this build compiled, "AVX-512", "AVX2", "SSE2", "NEON", "RVV" or
 * "scalar", for a host to report. */
const char *ifx_convert_simd(void);

#endif /* IFX_CONVERT_H */
