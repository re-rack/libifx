#include "ifx_hush_design.h"

#include "ifx_tables.h"

void ifx_hush_design_words_make(int threshold, int release, ifx_hush_design_words *words) {
  words->threshold = ifx_hush_threshold_table[threshold & 0x1F];
  words->slope = ifx_hush_slope_table[threshold & 0x1F];
  words->release = ifx_hush_release_table[release & 0x0F];
}
