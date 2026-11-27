/* DSP56001 data-ALU arithmetic as the Intellifex firmware executes it, for the
 * kernels' fixed-point work. An accumulator is a 56-bit two's complement value
 * in an int64_t: bits 55..48 the extension, 47..24 the high word, 23..0 the low
 * word. A 24-bit word is Q23 fixed point.
 *
 * The firmware keeps SR's scaling bits S1/S0 clear (boot's `andi #$fd,mr` only
 * clears I1), so rounding is the no-scaling half-to-even step and a move out of
 * an accumulator limits only when its extension is in use.
 */
#ifndef IFX_DSP_MATH_H
#define IFX_DSP_MATH_H

#include <stdint.h>

enum { IFX_W24_MAX = 0x7FFFFF, IFX_W24_MIN = -0x800000 };

#define IFX_A1_FIELD 0xFFFFFF000000ULL

#ifndef __cplusplus /* the library is compiled as C; C++ hosts only include this */
// cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
_Static_assert((-1 >> 1) == -1, "ifx_asr needs an arithmetic right shift");
#endif

/* The DSP56000 AGU's post-update of a 16-bit address with modulo M: M+1 words
 * in the next power-of-two window; a delta that is a multiple of the window
 * steps linearly. */
static inline uint32_t ifx_agu_step(uint32_t address, int32_t delta, uint32_t m) {
  address &= 0xFFFFu;
  const uint32_t modulo = m + 1u;
  uint32_t bufmask = m;
  bufmask |= bufmask >> 1u;
  bufmask |= bufmask >> 2u;
  bufmask |= bufmask >> 4u;
  bufmask |= bufmask >> 8u;
  bufmask |= bufmask >> 16u;
  const uint32_t magnitude = delta < 0 ? 0u - (uint32_t)delta : (uint32_t)delta;
  uint32_t reg = address + (uint32_t)delta;
  /* A step shorter than the window wraps at most once. */
  if (delta != 0 && magnitude <= bufmask) {
    if (modulo == bufmask + 1u) {
      return ((address & ~bufmask) | (reg & bufmask)) & 0xFFFFu;
    }
    const int32_t offset = (int32_t)(address & bufmask) + delta;
    if (offset > (int32_t)m) {
      reg -= modulo;
    } else if (offset < 0) {
      reg += modulo;
    }
    return reg & 0xFFFFu;
  }
  const uint32_t low = (address | 0x10000u) & ~bufmask;
  const uint32_t high = low + modulo - 1u;
  reg = (address | 0x10000u) + (uint32_t)delta;
  if ((magnitude & bufmask) != 0u) {
    if (reg > high) {
      reg -= modulo;
    } else if (reg < low) {
      reg += modulo;
    }
  }
  return reg & 0xFFFFu;
}

/* A 24-bit word as a signed value. */
static inline int32_t ifx_s24(uint32_t word) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return (int32_t)(word << 8) >> 8;
}

/* Wrap into the signed 56-bit accumulator range. */
static inline int64_t ifx_acc56(int64_t value) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return (int64_t)((uint64_t)value << 8) >> 8;
}

/* Floor division by 2^24: the accumulator's A1 field. */
static inline int64_t ifx_floor_step24(int64_t value) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return value >> 24;
}

/* MPYR/MACR/RND without scaling: half to even onto the 24-bit grid. Adding
 * 2^23 rounds half up; only a tie leaves the low 24 bits zero, and clearing bit
 * 24 then turns it to the even neighbour. */
static inline int64_t ifx_round24(int64_t value) {
  int64_t biased = value + 0x800000LL;
  if ((biased & 0xFFFFFFLL) == 0) {
    biased &= ~0x1000000LL;
  }
  return biased & ~0xFFFFFFLL;
}

/* The product of two words, as MPY and MAC add it. The 56-bit wrap is modular,
 * so a run of MACs may sum products and wrap once, where the last one rounds
 * or stores. */
static inline int64_t ifx_product(int32_t s1, int32_t s2) { return 2 * (int64_t)s1 * (int64_t)s2; }

/* MPY, MPYR, MAC and MACR. The instruction's sign is carried by the operand. */
static inline int64_t ifx_mpy(int32_t s1, int32_t s2) { return ifx_acc56(ifx_product(s1, s2)); }

static inline int64_t ifx_mpyr(int32_t s1, int32_t s2) { return ifx_round24(ifx_product(s1, s2)); }

/* MPYR with the $7FFFFF unity word: identity except words past half scale
 * lose an LSB. */
static inline int32_t ifx_mpyr_unity(int32_t word) {
  if (word > 0x400000) {
    return word - 1;
  }
  return word < -0x400000 ? word + 1 : word;
}

static inline int64_t ifx_mac(int64_t acc, int32_t s1, int32_t s2) {
  return ifx_acc56(acc + ifx_product(s1, s2));
}

static inline int64_t ifx_macr(int64_t acc, int32_t s1, int32_t s2) {
  return ifx_round24(ifx_acc56(acc + ifx_product(s1, s2)));
}

/* ADD and SUB of two accumulators, in mathematical order. */
static inline int64_t ifx_add(int64_t left, int64_t right) { return ifx_acc56(left + right); }

static inline int64_t ifx_sub(int64_t left, int64_t right) { return ifx_acc56(left - right); }

/* The accumulator value a 24-bit operand is read as: sign extended into A1. */
static inline int64_t ifx_word_acc(int32_t word) { return (int64_t)word * 0x1000000; }

/* ADD and SUB with a 24-bit register or memory operand. */
static inline int64_t ifx_add24(int64_t acc, int32_t word) {
  return ifx_acc56(acc + ifx_word_acc(word));
}

static inline int64_t ifx_sub24(int64_t acc, int32_t word) {
  return ifx_acc56(acc - ifx_word_acc(word));
}

/* ADDL: destination doubled plus source, wrapping. */
static inline int64_t ifx_addl(int64_t source, int64_t destination) {
  return ifx_acc56(source + destination + destination);
}

/* ASL and ASR shift the whole accumulator, wrapping and sign filling. */
static inline int64_t ifx_asl(int64_t acc) { return ifx_acc56(acc + acc); }

static inline int64_t ifx_asr(int64_t acc) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return acc >> 1;
}

static inline int64_t ifx_asl_n(int64_t acc, unsigned n) {
  return ifx_acc56((int64_t)(((uint64_t)acc * ((uint64_t)1 << n)) & 0xFFFFFFFFFFFFFFULL));
}

static inline int64_t ifx_asr_n(int64_t acc, unsigned n) {
  // cppcheck-suppress shiftNegativeLHS -- DSP ASR, defined by GCC/Clang
  return acc >> n;
}

/* LSL and LSR shift only the A1 field; the extension and low word keep their
 * bits. */
static inline int64_t ifx_lsl_a1(int64_t acc) {
  const uint64_t u = (uint64_t)ifx_acc56(acc);
  return (int64_t)((u & ~IFX_A1_FIELD) | (((u & IFX_A1_FIELD) << 1) & IFX_A1_FIELD));
}

static inline int64_t ifx_lsr_a1(int64_t acc) {
  const uint64_t u = (uint64_t)ifx_acc56(acc);
  return (int64_t)((u & ~IFX_A1_FIELD) | ((u & IFX_A1_FIELD) >> 1 & IFX_A1_FIELD));
}

/* AND with a 24-bit operand masks the A1 field only. */
static inline int64_t ifx_and_a1(int64_t acc, int32_t word) {
  const uint64_t keep = ~IFX_A1_FIELD | ((uint64_t)((uint32_t)word & 0xFFFFFFu) << 24);
  return (int64_t)((uint64_t)ifx_acc56(acc) & keep);
}

/* ABS and NEG wrap the 56-bit accumulator; the most negative value stays put. */
static inline int64_t ifx_neg56(int64_t acc) { return ifx_acc56(-acc); }

static inline int64_t ifx_abs56(int64_t acc) { return acc < 0 ? ifx_neg56(acc) : acc; }

/* The extension bits are in use outside the range a 24-bit word at A1 holds. */
static inline int ifx_es(int64_t acc) { return acc > 0x7FFFFFFFFFFFLL || acc < -0x800000000000LL; }

/* The A1, A0 and A2 fields as the bit patterns a move reads. */
static inline int32_t ifx_a1(int64_t acc) { return (int32_t)(((uint64_t)acc >> 24) & 0xFFFFFFu); }

static inline int32_t ifx_a0(int64_t acc) { return (int32_t)((uint64_t)acc & 0xFFFFFFu); }

static inline int32_t ifx_a2(int64_t acc) { return (int32_t)(((uint64_t)acc >> 48) & 0xFFu); }

/* `move a1,x0` and `move a0,x0`: the field as a signed word, never limited. */
static inline int32_t ifx_move_a1(int64_t acc) { return ifx_s24((uint32_t)((uint64_t)acc >> 24)); }

static inline int32_t ifx_move_a0(int64_t acc) { return ifx_s24((uint32_t)acc); }

/* A move into one field of an accumulator; the other fields keep their bits. */
static inline int64_t ifx_set_a2(int64_t acc, int32_t word) {
  return ifx_acc56((acc & 0xFFFFFFFFFFFFLL) | ((int64_t)(word & 0xFF) * 0x1000000000000LL));
}

static inline int64_t ifx_set_a1(int64_t acc, int32_t word) {
  const uint64_t u = (uint64_t)ifx_acc56(acc) & ~IFX_A1_FIELD;
  return (int64_t)(u | ((uint64_t)((uint32_t)word & 0xFFFFFFu) << 24));
}

static inline int64_t ifx_set_a0(int64_t acc, int32_t word) {
  const uint64_t u = (uint64_t)ifx_acc56(acc) & ~0xFFFFFFULL;
  return (int64_t)(u | ((uint32_t)word & 0xFFFFFFu));
}

/* A move out of an accumulator with its extension in use limits to the 24-bit
 * range; otherwise it stores A1. This is what `move a,x0` and `move a,x:ea`
 * produce at SR scaling 0. */
static inline int32_t ifx_store24(int64_t acc) {
  int64_t word = ifx_floor_step24(acc);
  if (word > IFX_W24_MAX) {
    return IFX_W24_MAX;
  }
  return word < IFX_W24_MIN ? IFX_W24_MIN : (int32_t)word;
}

#endif /* IFX_DSP_MATH_H */
