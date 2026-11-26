/* The Intellifex LTD as one board: the 80C32, the DSP56001 behind its host
 * port, the analogue converter, the battery and the front panel, coupled the
 * way the hardware couples them. Every panel action is driven the way a hand
 * drives it, so the firmware's own scan, host messages and block schedule
 * decide the result.
 *
 * Parameters are the MCU's, not the DSP's: `index` is the firmware's parameter
 * number (0..107, the offset into its value window) and `value` is what the
 * front panel shows for it. */
#ifndef IFX_EMU_H
#define IFX_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ifx_dsp_board.h"
#include "ifx_mcu_board.h"

#define IFX_EMU_EPROM_SIZE 0x8000u
#define IFX_EMU_PRESET_COUNT 160u
#define IFX_EMU_PARAMETER_COUNT 108u

typedef struct ifx_emu ifx_emu;

/* One host message as the DSP saw it: the words the MCU left in the ring, and
 * the block that drained them. `frame_applied` stays 0 while the words wait. */
typedef struct {
  uint64_t frame_in;
  uint64_t frame_applied;
  unsigned words;
} ifx_emu_message;

typedef struct {
  const int32_t *samples; /* Q23, one per frame; NULL is silence */
  size_t frames;
  /* The A/D is a 16-bit converter: the sample's low 8 bits never reach the
   * DSP's receive register. False hands the 24 bits over whole, which only
   * survives an SSI programmed for 24-bit words. */
  bool adc16;
} ifx_emu_feed;

/* Finds the EPROM by SHA-256 under `root` (NULL: the current directory) and in
 * root/reverse_engineering/inputs, boots from the factory battery, and returns
 * the board once the DSP runs. NULL, with the reason on stderr, if it cannot. */
ifx_emu *ifx_emu_create(const char *root);
void ifx_emu_destroy(ifx_emu *emu);

/* The two boards, for readback a caller wants beyond the accessors below. */
ifx_mcu_board *ifx_emu_mcu(const ifx_emu *emu);
ifx_dsp_board *ifx_emu_dsp(const ifx_emu *emu);
const char *ifx_emu_eprom_path(const ifx_emu *emu);

bool ifx_emu_ready(const ifx_emu *emu);
uint64_t ifx_emu_frames(const ifx_emu *emu);
uint64_t ifx_emu_clocks(const ifx_emu *emu);

/* Panel, one detent at a time, with the time the firmware needs in between. */
void ifx_emu_run_ms(ifx_emu *emu, unsigned ms);
void ifx_emu_turn(ifx_emu *emu, ifx_mcu_board_encoder encoder, int detents);
void ifx_emu_button(ifx_emu *emu, ifx_mcu_board_button button, bool pressed);

/* Recall a preset, then bring one of its parameters onto the panel and set it.
 * Both return false when the firmware refuses, and leave the panel where the
 * search stopped. */
bool ifx_emu_recall(ifx_emu *emu, unsigned preset);
bool ifx_emu_select(ifx_emu *emu, unsigned index);
bool ifx_emu_set_parameter(ifx_emu *emu, unsigned index, unsigned value);

void ifx_emu_display(const ifx_emu *emu, char *text);
unsigned ifx_emu_preset(const ifx_emu *emu);  /* 1..160 */
unsigned ifx_emu_config(const ifx_emu *emu);  /* 0..7, the DSP image the preset asks for */
unsigned ifx_emu_page(const ifx_emu *emu);    /* the function page on the panel */
unsigned ifx_emu_index(const ifx_emu *emu);   /* the parameter on the panel */
unsigned ifx_emu_value(const ifx_emu *emu, unsigned index);
void ifx_emu_host_traffic(const ifx_emu *emu, unsigned *writes, unsigned *reads);

/* Run `feed->frames` sample periods, feeding the converter and storing what the
 * DAC latches. `left` and `right` may be NULL to discard a channel. */
bool ifx_emu_audio(ifx_emu *emu, const ifx_emu_feed *feed, int32_t *left, int32_t *right);
unsigned ifx_emu_overruns(const ifx_emu *emu);
unsigned ifx_emu_underruns(const ifx_emu *emu);

const ifx_emu_message *ifx_emu_messages(const ifx_emu *emu, unsigned *count);
void ifx_emu_messages_clear(ifx_emu *emu);

void ifx_emu_reset(ifx_emu *emu);
void ifx_emu_battery_erase(ifx_emu *emu);
void ifx_emu_battery_factory(ifx_emu *emu);
const uint8_t *ifx_emu_battery(const ifx_emu *emu);
bool ifx_emu_battery_save(const ifx_emu *emu, const char *path);
bool ifx_emu_battery_load(ifx_emu *emu, const char *path);

/* The panel's own FACTORY RESTORE: recall preset 160, dial its ENTER CODE
 * parameter to 249, erase the battery and store. The restore page only exists
 * on preset 160's page list, so the battery has to boot that far first. */
bool ifx_emu_factory_restore(ifx_emu *emu);

#endif
