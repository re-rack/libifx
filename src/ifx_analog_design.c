#include "ifx_analog_design.h"

#include "ifx_tables.h"

int32_t ifx_analog_design_latch(int analog_direct, int direct_hush, int level) {
  return analog_direct != 0 && direct_hush == 0 ? ifx_direct_table[level] : 0;
}

int32_t ifx_analog_design_output(int level) { return (level * IFX_ANALOG_DESIGN_UNITY + 50) / 100; }
