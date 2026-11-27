/* The Intellifex LTD's MCU side: a Siemens SAB 80C32 (8052 core) on a 12 MHz
 * crystal, its EPROM, the battery-backed RAM, the front panel and the DSP's
 * host port.
 *
 * The panel is two active-low input bytes: P1 carries the PRESET knob and
 * three buttons, X:8002 the other three knobs and two buttons. Each knob is a
 * quadrature pair; the scan in the timer 0 interrupt counts one step per
 * transition, so ifx_mcu_board_panel_turn() moves one transition per scan.
 */
#ifndef IFX_MCU_BOARD_H
#define IFX_MCU_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* EPROM: 32 KiB of code and data, the eight DSP images included. */
#define IFX_MCU_BOARD_CODE_SIZE 0x10000u
/* Battery RAM: user presets, names, controller maps and the settings bytes. */
#define IFX_MCU_BOARD_BATTERY_SIZE 0x2000u
/* 16 characters of 14-segment display. */
#define IFX_MCU_BOARD_DISPLAY_WIDTH 16u
#define IFX_MCU_BOARD_XTAL_HZ 12000000u
#define IFX_MCU_BOARD_CLOCKS_PER_CYCLE 12u

typedef struct ifx_mcu_board ifx_mcu_board;

typedef enum {
  IFX_MCU_BOARD_PRESET = 0,
  IFX_MCU_BOARD_FUNCTION_SELECT = 1,
  IFX_MCU_BOARD_PARAMETER_SELECT = 2,
  IFX_MCU_BOARD_PARAMETER_ADJUST = 3,
  IFX_MCU_BOARD_ENCODER_COUNT = 4
} ifx_mcu_board_encoder;

typedef enum {
  IFX_MCU_BOARD_BUTTON_RECALL = 0,
  IFX_MCU_BOARD_BUTTON_STORE = 1,
  IFX_MCU_BOARD_BUTTON_COMPARE = 2,
  IFX_MCU_BOARD_BUTTON_CONFIG = 3,
  IFX_MCU_BOARD_BUTTON_BYPASS = 4,
  IFX_MCU_BOARD_BUTTON_COUNT = 5
} ifx_mcu_board_button;

/* The DSP as the MCU sees it: `reg` is the DSP56001 host port register the
 * MCU selects with P2 = 0x80 + reg and R0 = 0 (0 ICR, 1 CVR, 2 ISR, 3 IVR,
 * 5..7 transmit or receive bytes). `reset` follows the /RESET line on P3.4. */
typedef struct {
  void *user;
  uint8_t (*read)(void *user, unsigned reg);
  void (*write)(void *user, unsigned reg, uint8_t value);
  void (*reset)(void *user, bool asserted);
} ifx_mcu_board_host_t;

ifx_mcu_board *ifx_mcu_board_create(void);
void ifx_mcu_board_destroy(ifx_mcu_board *board);

/* The whole EPROM image, not just the MCU half: the firmware copies the DSP
 * bootstrap and images out of 0x4AF0. */
bool ifx_mcu_board_load_firmware(ifx_mcu_board *board, const uint8_t *image, size_t size);
void ifx_mcu_board_set_host(ifx_mcu_board *board, const ifx_mcu_board_host_t *host);

/* Reset from the reset vector with the battery RAM and the panel kept. */
void ifx_mcu_board_reset(ifx_mcu_board *board);

/* Run until at least `cycles` MCU clocks have elapsed (12 per machine cycle);
 * returns the clocks actually run. */
uint64_t ifx_mcu_board_step(ifx_mcu_board *board, uint64_t cycles);

void ifx_mcu_board_panel_turn(ifx_mcu_board *board, ifx_mcu_board_encoder encoder, int steps);
void ifx_mcu_board_panel_button(ifx_mcu_board *board, ifx_mcu_board_button button, bool pressed);

/* The 16 characters the display scan last drew, NUL terminated. */
void ifx_mcu_board_display(const ifx_mcu_board *board, char *text);
uint8_t ifx_mcu_board_leds(const ifx_mcu_board *board);

/* Battery RAM as the firmware leaves it; a unit with a dead battery reads
 * zero and reports EXT MEMORY ERROR until the tables are restored. */
uint8_t *ifx_mcu_board_battery(ifx_mcu_board *board);
void ifx_mcu_board_battery_erase(ifx_mcu_board *board);
void ifx_mcu_board_battery_factory(ifx_mcu_board *board);
bool ifx_mcu_board_battery_save(const ifx_mcu_board *board, const char *path);
bool ifx_mcu_board_battery_load(ifx_mcu_board *board, const char *path);

/* A synthetic firmware call needs the CPU's whole register state and IRAM. */
typedef struct {
  uint16_t pc;
  uint16_t dptr;
  uint8_t acc;
  uint8_t b;
  uint8_t psw;
  uint8_t sp;
  uint8_t regs[8];
} ifx_mcu_board_cpu_state;

void ifx_mcu_board_cpu_save(const ifx_mcu_board *board, ifx_mcu_board_cpu_state *state);
void ifx_mcu_board_cpu_restore(ifx_mcu_board *board, const ifx_mcu_board_cpu_state *state);
void ifx_mcu_board_set_iram(ifx_mcu_board *board, uint8_t addr, uint8_t value);
bool ifx_mcu_board_in_isr(const ifx_mcu_board *board);

/* State for bring-up logs and checks. */
uint16_t ifx_mcu_board_pc(const ifx_mcu_board *board);
uint8_t ifx_mcu_board_iram(const ifx_mcu_board *board, uint8_t addr);
uint8_t ifx_mcu_board_xdata(const ifx_mcu_board *board, uint16_t addr);

#endif /* IFX_MCU_BOARD_H */
