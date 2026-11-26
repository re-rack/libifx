/* Checks the memory map the vendored DSP core presents to the Intellifex board.
 *
 * The core is Hatari's, with the Falcon's split P/X/Y external RAM replaced by
 * the one 64K-word RAM the board carries; everything else is meant to stay as
 * upstream wrote it. This program pins that map down so a re-vendor cannot move
 * a word behind the firmware's back. */
#include <stdint.h>
#include <stdio.h>

#include "dsp_core.h"
#include "dsp_cpu.h"

static unsigned failures;

static void expect_u32(const char *what, uint32_t got, uint32_t wanted) {
  if (got != wanted) {
    printf("  %s: got %06x, wanted %06x\n", what, (unsigned)got, (unsigned)wanted);
    ++failures;
  }
}

static uint32_t first_x(void) {
  uint32_t a = 0;
  for (a = 0; a < 0x100u; ++a) {
    if (dsp56k_read_memory(DSP_SPACE_X, a) != 0u) {
      break;
    }
  }
  return a;
}

/* The board has one external RAM: a word written through any space is read back
 * through the other two at the same address. */
static void check_aliasing(void) {
  dsp56k_write_memory(DSP_SPACE_P, 0x0F80u, 0x123456u);
  expect_u32("P:$0F80 read as X", dsp56k_read_memory(DSP_SPACE_X, 0x0F80u), 0x123456u);
  expect_u32("P:$0F80 read as Y", dsp56k_read_memory(DSP_SPACE_Y, 0x0F80u), 0x123456u);
  dsp56k_write_memory(DSP_SPACE_Y, 0x0F80u, 0x654321u);
  expect_u32("Y:$0F80 read as P", dsp56k_read_memory(DSP_SPACE_P, 0x0F80u), 0x654321u);
  dsp56k_write_memory(DSP_SPACE_X, 0x0200u, 0xFFFFFFu);
  expect_u32("X:$0200 read as Y", dsp56k_read_memory(DSP_SPACE_Y, 0x0200u), 0xFFFFFFu);
  dsp56k_write_memory(DSP_SPACE_P, 0x0F81u, 0xABCDEF7Eu);
  expect_u32("outside the 24-bit word", dsp56k_read_memory(DSP_SPACE_P, 0x0F81u), 0xCDEF7Eu);
}

/* The 512 internal X words and 256 internal Y words are separate memories, and
 * separate from program memory. */
static void check_internal(void) {
  dsp56k_write_memory(DSP_SPACE_X, 0x0010u, 0x111111u);
  expect_u32("X internal held", dsp56k_read_memory(DSP_SPACE_X, 0x0010u), 0x111111u);
  expect_u32("X internal leaked into Y", dsp56k_read_memory(DSP_SPACE_Y, 0x0010u), 0u);
  expect_u32("X internal leaked into P", dsp56k_read_memory(DSP_SPACE_P, 0x0010u), 0u);
  dsp56k_write_memory(DSP_SPACE_Y, 0x0010u, 0x222222u);
  expect_u32("Y internal held", dsp56k_read_memory(DSP_SPACE_Y, 0x0010u), 0x222222u);

  dsp56k_write_memory(DSP_SPACE_P, 0x01FFu, 0x333333u);
  expect_u32("P internal held", dsp56k_read_memory(DSP_SPACE_P, 0x01FFu), 0x333333u);
  expect_u32("P internal leaked into X", dsp56k_read_memory(DSP_SPACE_X, 0x01FFu), 0u);
  dsp56k_write_memory(DSP_SPACE_P, 0x0200u, 0x444444u);
  expect_u32("P external held", dsp56k_read_memory(DSP_SPACE_P, 0x0200u), 0x444444u);
}

/* X:$0100..$01FF is the on-chip data ROM only while OMR.DE is set; the firmware
 * never sets it, so there the addresses fall through to external RAM. */
static void check_data_rom(void) {
  uint32_t saved = dsp_core.registers[DSP_REG_OMR];

  dsp_core.registers[DSP_REG_OMR] = 0u;
  dsp56k_write_memory(DSP_SPACE_X, 0x0123u, 0x555555u);
  expect_u32("X:$0123 with DE clear", dsp56k_read_memory(DSP_SPACE_X, 0x0123u), 0x555555u);

  dsp_core.rom[DSP_SPACE_X][0x0123u] = 0x666666u;
  dsp_core.registers[DSP_REG_OMR] = 1u << DSP_OMR_DE;
  expect_u32("X:$0123 with DE set", dsp56k_read_memory(DSP_SPACE_X, 0x0123u), 0x666666u);
  dsp56k_write_memory(DSP_SPACE_X, 0x0123u, 0x777777u);
  expect_u32("X:$0123 write with DE set", dsp56k_read_memory(DSP_SPACE_X, 0x0123u), 0x666666u);

  dsp_core.registers[DSP_REG_OMR] = saved;
}

/* The peripheral arrays sit at the top of X and Y space, so neither is aliased
 * there; program space has no peripherals and keeps its external words. */
static void check_peripherals(void) {
  dsp56k_write_memory(DSP_SPACE_X, 0xFFF0u, 0x123456u);
  expect_u32("X:$FFF0 peripheral", dsp56k_read_memory(DSP_SPACE_X, 0xFFF0u), 0x123456u);
  expect_u32("X:$FFF0 across spaces", dsp56k_read_memory(DSP_SPACE_P, 0xFFF0u), 0u);
  dsp56k_write_memory(DSP_SPACE_Y, 0xFFC0u, 0x234567u);
  expect_u32("Y:$FFC0 peripheral", dsp56k_read_memory(DSP_SPACE_Y, 0xFFC0u), 0x234567u);
  expect_u32("Y:$FFC0 across spaces", dsp56k_read_memory(DSP_SPACE_P, 0xFFC0u), 0u);
  dsp56k_write_memory(DSP_SPACE_P, 0xFFC0u, 0xABCDEFu);
  expect_u32("P:$FFC0 external RAM", dsp56k_read_memory(DSP_SPACE_P, 0xFFC0u), 0xABCDEFu);
}

static void ignore_host_interrupt(int state) { (void)state; }

int main(void) {
  dsp_core_init(ignore_host_interrupt);
  dsp56k_init_cpu();
  dsp_core_reset();
  if (first_x() != 0x100u) {
    printf("  internal X memory does not start empty\n");
    ++failures;
  }

  check_aliasing();
  check_internal();
  check_data_rom();
  check_peripherals();

  dsp_core_shutdown();
  printf("dsp core memory map: %s\n", failures == 0u ? "ok" : "FAILED");
  return failures == 0u ? 0 : 1;
}
