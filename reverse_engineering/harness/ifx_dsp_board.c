#include "ifx_dsp_board.h"

#include <stdlib.h>
#include <string.h>

#include "hatari/dsp_core.h"
#include "hatari/dsp_cpu.h"

struct ifx_dsp_board {
  uint64_t cycles;
  uint64_t frames;
  uint64_t passes;
  uint16_t last_pc;
  uint32_t breakpoint;
  bool break_hit;
  unsigned receive_overruns;
  unsigned transmit_underruns;
  bool host_request;
  int32_t last_left;
  int32_t last_right;
};

/* The core calls its host interrupt through a bare function pointer. */
static ifx_dsp_board *ifx_dsp_board_current;

static void ifx_dsp_board_host_interrupt(int state) {
  if (ifx_dsp_board_current != NULL) {
    ifx_dsp_board_current->host_request = state != 0;
  }
}

static uint32_t ifx_dsp_board_ssi_status(void) { return dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR]; }

ifx_dsp_board *ifx_dsp_board_create(void) {
  if (ifx_dsp_board_current != NULL) {
    return NULL;
  }
  ifx_dsp_board *board = calloc(1, sizeof(*board));
  if (board == NULL) {
    return NULL;
  }
  ifx_dsp_board_current = board;
  dsp_core_init(ifx_dsp_board_host_interrupt);
  dsp56k_init_cpu();
  dsp_core_reset();
  return board;
}

void ifx_dsp_board_destroy(ifx_dsp_board *board) {
  if (board == NULL) {
    return;
  }
  dsp_core_shutdown();
  if (ifx_dsp_board_current == board) {
    ifx_dsp_board_current = NULL;
  }
  free(board);
}

void ifx_dsp_board_reset(ifx_dsp_board *board) {
  dsp_core_reset();
  board->cycles = 0;
  board->frames = 0;
  board->passes = 0;
  board->last_pc = 0;
  board->breakpoint = 0xFFFFFFFFu;
  board->break_hit = false;
  board->receive_overruns = 0;
  board->transmit_underruns = 0;
  board->host_request = false;
  board->last_left = 0;
  board->last_right = 0;
}

void ifx_dsp_board_clear_memory(ifx_dsp_board *board) {
  (void)board;
  memset(dsp_core.ramint, 0, sizeof(dsp_core.ramint));
  memset(dsp_core.ramext, 0, sizeof(dsp_core.ramext));
}

void ifx_dsp_board_host_write(ifx_dsp_board *board, unsigned reg, uint8_t value) {
  (void)board;
  dsp_core_write_host((int)reg, value);
}

uint8_t ifx_dsp_board_host_read(const ifx_dsp_board *board, unsigned reg) {
  (void)board;
  return dsp_core_read_host((int)reg);
}

bool ifx_dsp_board_host_transmit_ready(const ifx_dsp_board *board) {
  return (ifx_dsp_board_host_read(board, CPU_HOST_ISR) & (1u << CPU_HOST_ISR_TXDE)) != 0;
}

bool ifx_dsp_board_host_receive_ready(const ifx_dsp_board *board) {
  return (ifx_dsp_board_host_read(board, CPU_HOST_ISR) & (1u << CPU_HOST_ISR_RXDF)) != 0;
}

void ifx_dsp_board_host_send(ifx_dsp_board *board, uint32_t word) {
  ifx_dsp_board_host_write(board, CPU_HOST_TRXH, (uint8_t)((word >> 16) & 0xffu));
  ifx_dsp_board_host_write(board, CPU_HOST_TRXM, (uint8_t)((word >> 8) & 0xffu));
  ifx_dsp_board_host_write(board, CPU_HOST_TRXL, (uint8_t)(word & 0xffu));
}

uint32_t ifx_dsp_board_host_receive(const ifx_dsp_board *board) {
  uint32_t value = (uint32_t)ifx_dsp_board_host_read(board, CPU_HOST_RXH) << 16;
  value |= (uint32_t)ifx_dsp_board_host_read(board, CPU_HOST_RXM) << 8;
  value |= ifx_dsp_board_host_read(board, CPU_HOST_RXL);
  return value;
}

bool ifx_dsp_board_host_request(const ifx_dsp_board *board) { return board->host_request; }

void ifx_dsp_board_step(ifx_dsp_board *board) {
  if (!dsp_core.running) {
    return;
  }
  if (dsp_core.pc == board->breakpoint) {
    board->break_hit = true;
    return;
  }
  if (dsp_core.pc == 0x0400u && board->last_pc != 0x0400u) {
    board->passes++;
  }
  board->last_pc = dsp_core.pc;
  dsp56k_execute_instruction();
  board->cycles += dsp_core.instr_cycle;
}

void ifx_dsp_board_run(ifx_dsp_board *board, uint32_t cycles) {
  uint32_t elapsed = 0;
  while (elapsed < cycles && dsp_core.running && !board->break_hit) {
    uint64_t before = board->cycles;
    ifx_dsp_board_step(board);
    elapsed += (uint32_t)(board->cycles - before);
  }
}

/* The transmit word the DSP wrote, before the SSI shifts it to its word length,
 * so a 16-bit frame still shows the bits below the converter's resolution. */
static int32_t ifx_dsp_board_transmit_word(void) {
  if (!dsp_core.running) {
    return 0;
  }
  bool enabled = dsp_core.ssi.crb_te && !dsp_core.ssi.waitFrameTX;
  uint32_t word = enabled ? (dsp_core.ssi.TX & 0xFFFFFFu) : 0u;
  return (int32_t)(word ^ 0x800000u) - (int32_t)0x800000;
}

static int32_t ifx_dsp_board_clock_transmit(ifx_dsp_board *board) {
  if (!dsp_core.running) {
    return 0;
  }
  /* TDE set: the register is empty, so this shift repeats the previous word. */
  if (dsp_core.ssi.crb_te && !dsp_core.ssi.waitFrameTX &&
      (ifx_dsp_board_ssi_status() & (1u << DSP_SSI_SR_TDE)) != 0) {
    board->transmit_underruns++;
  }
  int32_t word = ifx_dsp_board_transmit_word();
  dsp_core_ssi_Receive_SCK();
  return word;
}

static void ifx_dsp_board_frame_in(ifx_dsp_board *board, uint32_t in, bool wide, int32_t *left,
                                   int32_t *right) {
  if (!dsp_core.running) {
    return;
  }
  if (dsp_core.ssi.crb_re) {
    /* RDF still set: the firmware did not read the previous word before this
     * one arrived. */
    if ((ifx_dsp_board_ssi_status() & (1u << DSP_SSI_SR_RDF)) != 0) {
      board->receive_overruns++;
    }
    uint16_t word_length = dsp_core.ssi.cra_word_length;
    if (wide) {
      dsp_core.ssi.cra_word_length = 24u;
    }
    dsp_core.ssi.received_value = in & (wide ? 0xFFFFFFu : 0xFFFFu);
    dsp_core_ssi_Receive_SC1(1);
    dsp_core_ssi_Receive_SC2(1);
    dsp_core_ssi_Receive_SC0();
    dsp_core.ssi.cra_word_length = word_length;
  }

  /* The two halves let the core service the transmit interrupt of the first
   * word before the second one clocks out. */
  ifx_dsp_board_run(board, IFX_DSP_BOARD_FRAME_CYCLES / 2u);
  int32_t l = ifx_dsp_board_clock_transmit(board);
  ifx_dsp_board_run(board, IFX_DSP_BOARD_FRAME_CYCLES - IFX_DSP_BOARD_FRAME_CYCLES / 2u);
  int32_t r = ifx_dsp_board_clock_transmit(board);
  board->last_left = l;
  board->last_right = r;
  board->frames++;
  if (left != NULL) {
    *left = l;
  }
  if (right != NULL) {
    *right = r;
  }
}

void ifx_dsp_board_frame(ifx_dsp_board *board, uint32_t in, int32_t *left, int32_t *right) {
  ifx_dsp_board_frame_in(board, in, false, left, right);
}

void ifx_dsp_board_frame_wide(ifx_dsp_board *board, uint32_t in, int32_t *left, int32_t *right) {
  ifx_dsp_board_frame_in(board, in, true, left, right);
}

unsigned ifx_dsp_board_receive_overruns(const ifx_dsp_board *board) {
  return board->receive_overruns;
}

void ifx_dsp_board_output(const ifx_dsp_board *board, int32_t *left, int32_t *right) {
  if (left != NULL) {
    *left = board->last_left;
  }
  if (right != NULL) {
    *right = board->last_right;
  }
}

unsigned ifx_dsp_board_transmit_underruns(const ifx_dsp_board *board) {
  return board->transmit_underruns;
}

uint64_t ifx_dsp_board_frames(const ifx_dsp_board *board) { return board->frames; }

uint64_t ifx_dsp_board_passes(const ifx_dsp_board *board) { return board->passes; }

uint64_t ifx_dsp_board_cycles(const ifx_dsp_board *board) { return board->cycles; }

uint32_t ifx_dsp_board_pc(const ifx_dsp_board *board) {
  (void)board;
  return dsp_core.pc;
}

void ifx_dsp_board_set_breakpoint(ifx_dsp_board *board, uint32_t pc) {
  board->breakpoint = pc;
  board->break_hit = false;
}

bool ifx_dsp_board_breakpoint_hit(const ifx_dsp_board *board) { return board->break_hit; }

void ifx_dsp_board_clear_breakpoint(ifx_dsp_board *board) {
  board->breakpoint = 0xFFFFFFFFu;
  board->break_hit = false;
}

bool ifx_dsp_board_running(const ifx_dsp_board *board) {
  (void)board;
  return dsp_core.running != 0;
}

uint32_t ifx_dsp_board_word(const ifx_dsp_board *board, unsigned space, uint32_t address) {
  (void)board;
  if (address >= 0xffc0u) {
    return dsp_core.periph[space == DSP_SPACE_P ? DSP_SPACE_X : space][address - 0xffc0u] &
           0xffffffu;
  }
  if (space == DSP_SPACE_P) {
    if (address < 0x200u) {
      return dsp_core.ramint[DSP_SPACE_P][address] & 0xffffffu;
    }
  } else if (address < 0x100u) {
    return dsp_core.ramint[space][address] & 0xffffffu;
  } else if (address < 0x200u && (dsp_core.registers[DSP_REG_OMR] & (1u << DSP_OMR_DE)) != 0) {
    return dsp_core.rom[space][address] & 0xffffffu;
  }
  return dsp_core.ramext[address & 0xffffu] & 0xffffffu;
}

void ifx_dsp_board_store(ifx_dsp_board *board, unsigned space, uint32_t address, uint32_t value) {
  (void)board;
  dsp56k_write_memory((int)space, address, value);
}

uint32_t ifx_dsp_board_omr(const ifx_dsp_board *board) {
  (void)board;
  return dsp_core.registers[DSP_REG_OMR];
}

void ifx_dsp_board_set_omr(ifx_dsp_board *board, uint32_t value) {
  (void)board;
  dsp_core.registers[DSP_REG_OMR] = value & 0xffu;
}

uint32_t ifx_dsp_board_bcr(const ifx_dsp_board *board) {
  (void)board;
  return dsp_core.periph[DSP_SPACE_X][DSP_BCR];
}

void ifx_dsp_board_set_bcr(ifx_dsp_board *board, uint32_t value) {
  (void)board;
  dsp_core.periph[DSP_SPACE_X][DSP_BCR] = value & 0xffffu;
}

uint32_t ifx_dsp_board_ipr(const ifx_dsp_board *board) {
  (void)board;
  return dsp_core.periph[DSP_SPACE_X][DSP_IPR];
}

void ifx_dsp_board_set_ipr(ifx_dsp_board *board, uint32_t value) {
  (void)board;
  dsp_core.periph[DSP_SPACE_X][DSP_IPR] = value & 0xffffu;
}

uint32_t ifx_dsp_board_port_register(const ifx_dsp_board *board, unsigned reg) {
  (void)board;
  return dsp_core.periph[DSP_SPACE_X][reg];
}

unsigned ifx_dsp_board_port_pins(const ifx_dsp_board *board, enum ifx_dsp_port port) {
  unsigned data = ifx_dsp_board_port_register(board, port == IFX_DSP_PORT_B ? (unsigned)DSP_PBD
                                                                            : (unsigned)DSP_PCD);
  unsigned direction = ifx_dsp_board_port_register(
      board, port == IFX_DSP_PORT_B ? (unsigned)DSP_PBDDR : (unsigned)DSP_PCDDR);
  return data & direction;
}

uint32_t ifx_dsp_board_reg(const ifx_dsp_board *board, unsigned reg) {
  (void)board;
  return reg < 64u ? dsp_core.registers[reg] : 0;
}

void ifx_dsp_board_set_reg(ifx_dsp_board *board, unsigned reg, uint32_t value) {
  (void)board;
  if (reg < 64u) {
    dsp_core.registers[reg] = value;
  }
}
