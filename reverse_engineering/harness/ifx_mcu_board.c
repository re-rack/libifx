#include "ifx_mcu_board.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "ports.h"
#include "timers.h"
#include "uart.h"

/* The 14-segment font: entry `(ch - 0x20) * 2` of the pair the scan writes to
 * X:8005 and X:8007, so the EPROM itself decodes the display. */
#define IFX_MCU_BOARD_FONT 0x1834u
#define IFX_MCU_BOARD_FONT_FIRST 0x20u
#define IFX_MCU_BOARD_FONT_LAST 0x7Fu

/* Host port: P2 = 0x80 + reg, R0 = 0. The panel and the display share the
 * 0x80 and 0x81 pages with the low address byte. */
#define IFX_MCU_BOARD_HOST_PAGE_FIRST 0x80u
#define IFX_MCU_BOARD_HOST_PAGE_LAST 0x87u

#define IFX_MCU_BOARD_XDATA_PANEL_OUT_A 0x8001u
#define IFX_MCU_BOARD_XDATA_PANEL_IN 0x8002u
#define IFX_MCU_BOARD_XDATA_LEDS 0x8003u
#define IFX_MCU_BOARD_XDATA_DIGIT_LO 0x8004u
#define IFX_MCU_BOARD_XDATA_SEG_LO 0x8005u
#define IFX_MCU_BOARD_XDATA_DIGIT_HI 0x8006u
#define IFX_MCU_BOARD_XDATA_SEG_HI 0x8007u
#define IFX_MCU_BOARD_XDATA_PANEL_OUT_B 0x8101u

typedef struct {
  bool on_p1;
  uint8_t clock_bit;
  uint8_t dir_bit;
  uint8_t phase;
  int pending;
} ifx_mcu_board_knob;

typedef struct {
  bool on_p1;
  uint8_t bit;
} ifx_mcu_board_switch;

/* Knob pairs: the higher bit is the line the scan watches for a change, the
 * lower one the direction. Buttons: RECALL, CONFIG and COMPARE sit on P1,
 * STORE and BYPASS on X:8002. */
static const bool k_knob_on_p1[IFX_MCU_BOARD_ENCODER_COUNT] = {true, false, false, false};
static const uint8_t k_knob_clock_bit[IFX_MCU_BOARD_ENCODER_COUNT] = {5, 1, 3, 6};
static const uint8_t k_knob_dir_bit[IFX_MCU_BOARD_ENCODER_COUNT] = {4, 2, 4, 7};
static const bool k_button_on_p1[IFX_MCU_BOARD_BUTTON_COUNT] = {true, false, true, true, false};
static const uint8_t k_button_bit[IFX_MCU_BOARD_BUTTON_COUNT] = {3, 5, 7, 6, 0};

/* One transition per step, clockwise then anticlockwise: the scan counts +1
 * for every transition of this order and -1 for its reverse. */
static const uint8_t k_quadrature_cw[4] = {2, 0, 3, 1};
static const uint8_t k_quadrature_ccw[4] = {1, 3, 0, 2};

struct ifx_mcu_board {
  cpu_t cpu;
  timers_t timers;
  uart_t uart;
  ports_t ports;
  timing_config_t timing;
  cpu_tick_entry_t tick_hooks[2];

  uint8_t code[IFX_MCU_BOARD_CODE_SIZE];
  uint8_t battery[IFX_MCU_BOARD_BATTERY_SIZE];

  uint8_t p1;
  uint8_t panel_in;
  uint8_t panel_out[IFX_MCU_BOARD_DIRECT_COUNT];
  uint8_t leds;
  uint8_t digit[2];
  uint8_t seg[2];
  char display[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];

  ifx_mcu_board_knob knobs[IFX_MCU_BOARD_ENCODER_COUNT];
  ifx_mcu_board_switch buttons[IFX_MCU_BOARD_BUTTON_COUNT];

  ifx_mcu_board_host_t host;
  bool dsp_reset;
  void (*midi_out)(void *user, uint8_t byte);
  void *midi_user;

  unsigned unmapped_reads;
  unsigned unmapped_writes;
};

static bool board_host_page(uint16_t addr, unsigned *reg) {
  unsigned page = (unsigned)(addr >> 8);
  if (page < IFX_MCU_BOARD_HOST_PAGE_FIRST || page > IFX_MCU_BOARD_HOST_PAGE_LAST) {
    return false;
  }
  if ((addr & 0xFFu) != 0u) {
    return false;
  }
  *reg = page & 0x07u;
  return true;
}

static char board_glyph(const ifx_mcu_board *board) {
  uint8_t hi = board->seg[0];
  uint8_t lo = board->seg[1];
  for (unsigned ch = IFX_MCU_BOARD_FONT_FIRST; ch <= IFX_MCU_BOARD_FONT_LAST; ++ch) {
    unsigned offset = IFX_MCU_BOARD_FONT + (ch - IFX_MCU_BOARD_FONT_FIRST) * 2u;
    if (board->code[offset] == hi && board->code[offset + 1u] == lo) {
      return (char)ch;
    }
  }
  return '?';
}

/* The scan enables one cell at a time: X:8004 drives cells 8-15 and X:8006
 * cells 0-7, each from bit 7 down. */
static void board_select_digit(ifx_mcu_board *board, unsigned half, uint8_t value) {
  if (board->digit[half] == value) {
    return;
  }
  board->digit[half] = value;
  unsigned base = half == 0u ? 8u : 0u;
  for (unsigned bit = 0; bit < 8u; ++bit) {
    if ((value & (uint8_t)(0x80u >> bit)) != 0u) {
      board->display[base + bit] = board_glyph(board);
      return;
    }
  }
}

static void board_scan_knob(ifx_mcu_board *board, ifx_mcu_board_encoder encoder) {
  ifx_mcu_board_knob *knob = &board->knobs[encoder];
  if (knob->pending == 0) {
    return;
  }
  bool forward = knob->pending > 0;
  uint8_t *byte = knob->on_p1 ? &board->p1 : &board->panel_in;
  uint8_t mask = (uint8_t)((1u << knob->clock_bit) | (1u << knob->dir_bit));

  knob->phase = forward ? k_quadrature_cw[knob->phase] : k_quadrature_ccw[knob->phase];
  knob->pending += forward ? -1 : 1;
  uint8_t bits = 0;
  if ((knob->phase & 1u) != 0u) {
    bits |= (uint8_t)(1u << knob->clock_bit);
  }
  if ((knob->phase & 2u) != 0u) {
    bits |= (uint8_t)(1u << knob->dir_bit);
  }
  *byte = (uint8_t)((*byte & (uint8_t)~mask) | bits);
}

static bool board_xdata_peek(const ifx_mcu_board *board, uint16_t addr, uint8_t *value) {
  switch (addr) {
  case IFX_MCU_BOARD_XDATA_PANEL_OUT_A:
    *value = board->panel_out[IFX_MCU_BOARD_DIRECT_LEFT];
    return true;
  case IFX_MCU_BOARD_XDATA_PANEL_IN:
    *value = (uint8_t)~board->panel_in;
    return true;
  case IFX_MCU_BOARD_XDATA_LEDS:
    *value = board->leds;
    return true;
  case IFX_MCU_BOARD_XDATA_DIGIT_LO:
    *value = board->digit[0];
    return true;
  case IFX_MCU_BOARD_XDATA_SEG_LO:
    *value = board->seg[0];
    return true;
  case IFX_MCU_BOARD_XDATA_DIGIT_HI:
    *value = board->digit[1];
    return true;
  case IFX_MCU_BOARD_XDATA_SEG_HI:
    *value = board->seg[1];
    return true;
  case IFX_MCU_BOARD_XDATA_PANEL_OUT_B:
    *value = board->panel_out[IFX_MCU_BOARD_DIRECT_RIGHT];
    return true;
  default:
    break;
  }
  if (addr < IFX_MCU_BOARD_BATTERY_SIZE) {
    *value = board->battery[addr];
    return true;
  }
  return false;
}

static uint8_t board_xdata_read(const cpu_t *cpu, uint16_t addr, void *user) {
  (void)cpu;
  ifx_mcu_board *board = user;
  unsigned reg = 0;
  if (board_host_page(addr, &reg)) {
    return board->host.read != NULL ? board->host.read(board->host.user, reg) : 0xFFu;
  }
  if (addr == IFX_MCU_BOARD_XDATA_PANEL_IN) {
    board_scan_knob(board, IFX_MCU_BOARD_FUNCTION_SELECT);
    board_scan_knob(board, IFX_MCU_BOARD_PARAMETER_SELECT);
    board_scan_knob(board, IFX_MCU_BOARD_PARAMETER_ADJUST);
    return (uint8_t)~board->panel_in;
  }
  uint8_t value = 0;
  if (board_xdata_peek(board, addr, &value)) {
    return value;
  }
  board->unmapped_reads++;
  return 0xFFu;
}

static void board_xdata_write(cpu_t *cpu, uint16_t addr, uint8_t value, void *user) {
  (void)cpu;
  ifx_mcu_board *board = user;
  unsigned reg = 0;
  if (board_host_page(addr, &reg)) {
    if (board->host.write != NULL) {
      board->host.write(board->host.user, reg, value);
    }
    return;
  }
  switch (addr) {
  case IFX_MCU_BOARD_XDATA_PANEL_OUT_A:
    board->panel_out[IFX_MCU_BOARD_DIRECT_LEFT] = value;
    return;
  case IFX_MCU_BOARD_XDATA_LEDS:
    board->leds = value;
    return;
  case IFX_MCU_BOARD_XDATA_DIGIT_LO:
    board_select_digit(board, 0, value);
    return;
  case IFX_MCU_BOARD_XDATA_SEG_LO:
    board->seg[0] = value;
    return;
  case IFX_MCU_BOARD_XDATA_DIGIT_HI:
    board_select_digit(board, 1, value);
    return;
  case IFX_MCU_BOARD_XDATA_SEG_HI:
    board->seg[1] = value;
    return;
  case IFX_MCU_BOARD_XDATA_PANEL_OUT_B:
    board->panel_out[IFX_MCU_BOARD_DIRECT_RIGHT] = value;
    return;
  default:
    break;
  }
  if (addr < IFX_MCU_BOARD_BATTERY_SIZE) {
    board->battery[addr] = value;
    return;
  }
  board->unmapped_writes++;
}

// cppcheck-suppress constParameterCallback
static uint8_t board_code_read(const cpu_t *cpu, uint16_t addr, void *user) {
  (void)cpu;
  const ifx_mcu_board *board = user;
  return board->code[addr];
}

static const cpu_mem_ops_t k_mem_ops = {
    .code_read = board_code_read,
    .code_write = NULL,
    .xdata_read = board_xdata_read,
    .xdata_write = board_xdata_write,
};

static uint8_t board_port_read(uint8_t port, void *user) {
  if (port == 1u) {
    ifx_mcu_board *board = user;
    board_scan_knob(board, IFX_MCU_BOARD_PRESET);
    return (uint8_t)~board->p1;
  }
  return 0xFFu;
}

static void board_port_write(uint8_t port, uint8_t level, uint8_t mask, void *user) {
  ifx_mcu_board *board = user;
  if (port != 3u || (mask & 0x10u) == 0u) {
    return;
  }
  bool asserted = (level & 0x10u) == 0u;
  if (asserted == board->dsp_reset) {
    return;
  }
  board->dsp_reset = asserted;
  if (board->host.reset != NULL) {
    board->host.reset(board->host.user, asserted);
  }
}

static void board_tick_timers(cpu_t *cpu, uint32_t cycles, void *user) {
  (void)cpu;
  timers_tick((timers_t *)user, cycles);
}

static void board_tick_uart(cpu_t *cpu, uint32_t cycles, void *user) {
  (void)cpu;
  uart_tick((uart_t *)user, cycles);
}

static void board_midi_transmit(uint8_t byte, void *user) {
  ifx_mcu_board *board = user;
  if (board->midi_out != NULL) {
    board->midi_out(board->midi_user, byte);
  }
}

static void board_wire(ifx_mcu_board *board) {
  board->tick_hooks[0].fn = board_tick_timers;
  board->tick_hooks[0].user = &board->timers;
  board->tick_hooks[1].fn = board_tick_uart;
  board->tick_hooks[1].user = &board->uart;

  uart_init(&board->uart, &board->timing);
  uart_set_tx_callback(&board->uart, board_midi_transmit, board);
  uart_attach(&board->cpu, &board->uart);
  timers_init(&board->timers, &board->cpu);
  ports_init(&board->ports, &board->cpu, board_port_read, board_port_write, board);
  cpu_set_tick_hooks(&board->cpu, board->tick_hooks, (uint8_t)MCS51_ARRAY_LEN(board->tick_hooks));
  cpu_set_mem_ops(&board->cpu, &k_mem_ops, board);
}

static void board_clear_panel(ifx_mcu_board *board) {
  board->p1 = 0;
  board->panel_in = 0;
  board->panel_out[IFX_MCU_BOARD_DIRECT_LEFT] = 0;
  board->panel_out[IFX_MCU_BOARD_DIRECT_RIGHT] = 0;
  board->leds = 0;
  board->digit[0] = 0;
  board->digit[1] = 0;
  board->seg[0] = 0;
  board->seg[1] = 0;
  board->dsp_reset = false;
  memset(board->display, ' ', IFX_MCU_BOARD_DISPLAY_WIDTH);
  board->display[IFX_MCU_BOARD_DISPLAY_WIDTH] = '\0';
  for (unsigned i = 0; i < IFX_MCU_BOARD_ENCODER_COUNT; ++i) {
    board->knobs[i].on_p1 = k_knob_on_p1[i];
    board->knobs[i].clock_bit = k_knob_clock_bit[i];
    board->knobs[i].dir_bit = k_knob_dir_bit[i];
    board->knobs[i].phase = 0;
    board->knobs[i].pending = 0;
  }
  for (unsigned i = 0; i < IFX_MCU_BOARD_BUTTON_COUNT; ++i) {
    board->buttons[i].on_p1 = k_button_on_p1[i];
    board->buttons[i].bit = k_button_bit[i];
  }
}

ifx_mcu_board *ifx_mcu_board_create(void) {
  ifx_mcu_board *board = calloc(1, sizeof(*board));
  if (board == NULL) {
    return NULL;
  }
  board->timing.fosc_hz = IFX_MCU_BOARD_XTAL_HZ;
  board->timing.clocks_per_cycle = (uint8_t)IFX_MCU_BOARD_CLOCKS_PER_CYCLE;
  memset(board->code, 0xFF, sizeof(board->code));
  board_clear_panel(board);
  cpu_init(&board->cpu);
  cpu_set_mem_ops(&board->cpu, &k_mem_ops, board);
  board_wire(board);
  cpu_reset(&board->cpu);
  return board;
}

void ifx_mcu_board_destroy(ifx_mcu_board *board) { free(board); }

bool ifx_mcu_board_load_firmware(ifx_mcu_board *board, const uint8_t *image, size_t size) {
  if (image == NULL || size > sizeof(board->code)) {
    return false;
  }
  memset(board->code, 0xFF, sizeof(board->code));
  memcpy(board->code, image, size);
  return true;
}

bool ifx_mcu_board_load_firmware_file(ifx_mcu_board *board, const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return false;
  }
  memset(board->code, 0xFF, sizeof(board->code));
  size_t read = fread(board->code, 1u, sizeof(board->code), file);
  bool ok = ferror(file) == 0 && (read < sizeof(board->code) || fgetc(file) == EOF);
  fclose(file);
  return ok;
}

void ifx_mcu_board_set_host(ifx_mcu_board *board, const ifx_mcu_board_host_t *host) {
  board->host = host != NULL ? *host : (ifx_mcu_board_host_t){0};
}

void ifx_mcu_board_reset(ifx_mcu_board *board) {
  board_clear_panel(board);
  cpu_reset(&board->cpu);
  board_wire(board);
  board->unmapped_reads = 0;
  board->unmapped_writes = 0;
}

uint64_t ifx_mcu_board_step(ifx_mcu_board *board, uint64_t clocks) {
  uint64_t cycles = (clocks + IFX_MCU_BOARD_CLOCKS_PER_CYCLE - 1u) / IFX_MCU_BOARD_CLOCKS_PER_CYCLE;
  uint64_t elapsed = 0;
  while (elapsed < cycles && !board->cpu.halted) {
    uint8_t step = cpu_step(&board->cpu);
    if (step == 0u) {
      if (!board->cpu.idle || board->cpu.power_down) {
        break;
      }
      step = 1u;
    }
    for (uint8_t i = 0; i < board->cpu.tick_hook_count; ++i) {
      const cpu_tick_entry_t *entry = &board->cpu.tick_hooks[i];
      if (entry->fn != NULL) {
        entry->fn(&board->cpu, step, entry->user);
      }
    }
    /* The tick just added this step's time; the prompt answer is the one
     * the timer 0 vector and the host port waits rely on. */
    cpu_poll_interrupts(&board->cpu);
    step = (uint8_t)(step + board->cpu.vector_cycles);
    board->cpu.vector_cycles = 0u;
    elapsed += step;
  }
  return elapsed * IFX_MCU_BOARD_CLOCKS_PER_CYCLE;
}

void ifx_mcu_board_panel_turn(ifx_mcu_board *board, ifx_mcu_board_encoder encoder, int steps) {
  if ((unsigned)encoder >= (unsigned)IFX_MCU_BOARD_ENCODER_COUNT) {
    return;
  }
  board->knobs[encoder].pending += steps;
}

void ifx_mcu_board_panel_button(ifx_mcu_board *board, ifx_mcu_board_button button, bool pressed) {
  if ((unsigned)button >= (unsigned)IFX_MCU_BOARD_BUTTON_COUNT) {
    return;
  }
  const ifx_mcu_board_switch *entry = &board->buttons[button];
  uint8_t *byte = entry->on_p1 ? &board->p1 : &board->panel_in;
  if (pressed) {
    *byte = (uint8_t)(*byte | (uint8_t)(1u << entry->bit));
  } else {
    *byte = (uint8_t)(*byte & (uint8_t)~(uint8_t)(1u << entry->bit));
  }
}

void ifx_mcu_board_display(const ifx_mcu_board *board, char *text) {
  memcpy(text, board->display, IFX_MCU_BOARD_DISPLAY_WIDTH + 1u);
}

uint8_t ifx_mcu_board_leds(const ifx_mcu_board *board) { return board->leds; }

uint8_t ifx_mcu_board_direct_level(const ifx_mcu_board *board, ifx_mcu_board_direct channel) {
  if ((unsigned)channel >= (unsigned)IFX_MCU_BOARD_DIRECT_COUNT) {
    return 0;
  }
  return board->panel_out[channel];
}

// cppcheck-suppress constParameterPointer
uint8_t *ifx_mcu_board_battery(ifx_mcu_board *board) { return board->battery; }

void ifx_mcu_board_battery_erase(ifx_mcu_board *board) {
  memset(board->battery, 0, sizeof(board->battery));
}

static void battery_copy(ifx_mcu_board *board, uint16_t dest, uint16_t src, uint16_t count) {
  for (uint16_t i = 0; i < count; ++i) {
    if ((uint32_t)dest + i < (uint32_t)sizeof(board->battery)) {
      board->battery[dest + i] = board->code[src + i];
    }
  }
}

void ifx_mcu_board_battery_factory(ifx_mcu_board *board) {
  /* The firmware's FACTORY RESTORE (C:108A, code 249): the program map filled
   * with its own numbering, the preset records, then the valid signature. */
  ifx_mcu_board_battery_erase(board);
  for (unsigned slot = 0; slot < 128u; ++slot) {
    board->battery[0x0001u + slot] = (uint8_t)slot;
  }
  battery_copy(board, 0x1BEAu, 0x1BCFu, 5u);
  for (unsigned preset = 0; preset < 80u; ++preset) {
    /* The restore's copy routine reloads its source pointer on every pass of the
     * outer loop, so all 80 preset headers come from the first 32 bytes. */
    battery_copy(board, (uint16_t)(0x0081u + preset * 32u), 0x2064u, 32u);
    uint16_t dest = (uint16_t)(0x0A81u + preset * 55u);
    uint16_t src = (uint16_t)(0x2084u + preset * 55u);
    battery_copy(board, dest, src, 55u);
    uint8_t config = (uint8_t)(board->code[src] & 0x07u);
    board->battery[dest] = config < 6u ? config : 0u;
  }
  battery_copy(board, 0x1BEFu, 0x1C54u, 1040u);
  board->battery[0x1BE9u] = 0x17u;
}

bool ifx_mcu_board_battery_save(const ifx_mcu_board *board, const char *path) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    return false;
  }
  size_t written = fwrite(board->battery, 1u, sizeof(board->battery), file);
  int closed = fclose(file);
  return written == sizeof(board->battery) && closed == 0;
}

bool ifx_mcu_board_battery_load(ifx_mcu_board *board, const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return false;
  }
  size_t read = fread(board->battery, 1u, sizeof(board->battery), file);
  bool ok = read == sizeof(board->battery);
  fclose(file);
  return ok;
}

void ifx_mcu_board_midi_handler(ifx_mcu_board *board, void (*fn)(void *user, uint8_t byte),
                                void *user) {
  board->midi_out = fn;
  board->midi_user = user;
}

bool ifx_mcu_board_midi_receive(ifx_mcu_board *board, uint8_t byte) {
  return uart_queue_rx_byte(&board->uart, byte);
}

uint16_t ifx_mcu_board_pc(const ifx_mcu_board *board) { return board->cpu.pc; }

static unsigned board_register_bank(const cpu_t *cpu) { return (cpu->psw >> 3) & 0x03u; }

void ifx_mcu_board_cpu_save(const ifx_mcu_board *board, ifx_mcu_board_cpu_state *state) {
  const cpu_t *cpu = &board->cpu;
  state->pc = cpu->pc;
  state->dptr = cpu->dptr;
  state->acc = cpu->acc;
  state->b = cpu->b;
  state->psw = cpu->psw;
  state->sp = cpu->sp;
  unsigned bank = board_register_bank(cpu) * 8u;
  for (unsigned i = 0; i < 8u; ++i) {
    state->regs[i] = cpu->iram[bank + i];
  }
}

void ifx_mcu_board_cpu_restore(ifx_mcu_board *board, const ifx_mcu_board_cpu_state *state) {
  cpu_t *cpu = &board->cpu;
  cpu->pc = state->pc;
  cpu->dptr = state->dptr;
  cpu->acc = state->acc;
  cpu->b = state->b;
  cpu->psw = state->psw;
  cpu->sp = state->sp;
  unsigned bank = board_register_bank(cpu) * 8u;
  for (unsigned i = 0; i < 8u; ++i) {
    cpu->iram[bank + i] = state->regs[i];
  }
}

void ifx_mcu_board_set_iram(ifx_mcu_board *board, uint8_t addr, uint8_t value) {
  board->cpu.iram[addr] = value;
}

bool ifx_mcu_board_in_isr(const ifx_mcu_board *board) {
  return board->cpu.in_isr || board->cpu.isr_depth != 0u;
}

uint8_t ifx_mcu_board_iram(const ifx_mcu_board *board, uint8_t addr) {
  return board->cpu.iram[addr];
}

uint8_t ifx_mcu_board_xdata(const ifx_mcu_board *board, uint16_t addr) {
  unsigned reg = 0;
  if (board_host_page(addr, &reg)) {
    return board->host.read != NULL ? board->host.read(board->host.user, reg) : 0xFFu;
  }
  uint8_t value = 0xFFu;
  board_xdata_peek(board, addr, &value);
  return value;
}

unsigned ifx_mcu_board_unmapped_reads(const ifx_mcu_board *board) { return board->unmapped_reads; }

unsigned ifx_mcu_board_unmapped_writes(const ifx_mcu_board *board) {
  return board->unmapped_writes;
}
