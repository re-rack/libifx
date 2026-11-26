#include "ifx_mixer_design.h"

#include "ifx_tables.h"

int32_t ifx_mixer_design_level(int level) { return ifx_level_table[level & 0x3F]; }
