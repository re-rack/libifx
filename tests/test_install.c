/* A host built against the installed library alone: ifx.h from the pkg-config
 * include path and libifx.a. make check-install compiles it as C and as C++. */
#include <stdio.h>

#include "ifx.h"

#ifdef __cplusplus
#define TEST_INSTALL_LANGUAGE "C++"
#else
#define TEST_INSTALL_LANGUAGE "C"
#endif

enum { TEST_INSTALL_FRAMES = 512 };

int main(void) {
  static float samples[TEST_INSTALL_FRAMES];
  static int32_t words[TEST_INSTALL_FRAMES];
  static float back[TEST_INSTALL_FRAMES];
  for (unsigned i = 0; i < TEST_INSTALL_FRAMES; i++) {
    samples[i] = ((float)((int)i - 256) / 512.0f);
  }
  ifx_f32_to_q23(words, samples, TEST_INSTALL_FRAMES);
  ifx_q23_to_f32(back, words, TEST_INSTALL_FRAMES);
  float energy = 0.0f;
  for (unsigned i = 0; i < TEST_INSTALL_FRAMES; i++) {
    energy += back[i] * back[i];
  }
  const int ok = energy > 0.0f;
  printf("install %-7s %s  ifx.h, %s conversion\n", TEST_INSTALL_LANGUAGE, ok ? "PASS" : "FAIL",
         ifx_convert_simd());
  return ok ? 0 : 1;
}
