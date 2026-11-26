#include "ifx_ducker_design.h"

#include "ifx_tables.h"

void ifx_ducker_design_words_make(int sensitivity, int attenuation, int release,
                                  ifx_ducker_design_words *words) {
  words->threshold = ifx_hush_threshold_table[sensitivity & 0x1F];
  words->attenuation = ifx_level_table[attenuation & 0x3F];
  words->release = ifx_ducker_release_table[release & 0x1F];
}
