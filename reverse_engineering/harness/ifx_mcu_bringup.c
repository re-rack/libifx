/* Bring-up of the MCU board with the DSP board behind its host port: the
 * 80C32 boots from the EPROM, talks to the DSP, and the front panel is driven
 * the way a hand would drive it.
 *
 * Usage: ifx_mcu_bringup [command ...]
 *   500          run 500 ms of emulated time
 *   show         print the panel, the RAM state and the traffic counters
 *   button:x     press and release button x (recall, store, compare, config,
 *                bypass)
 *   press:x      press and hold x
 *   release:x    release x
 *   turn:x:n     turn knob x (preset, function, parameter, adjust) n detents
 *   factory      restore the battery RAM from the EPROM tables
 *   erase        zero the battery RAM
 *   dump         print the display buffer and the preset name registers
 *   save:path    write the battery RAM out
 *   load:path    read the battery RAM back
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_cpu.h"
#include "ifx_dsp_board.h"
#include "ifx_mcu_board.h"

#define IFX_EPROM_SIZE 0x8000u
#define IFX_FIRMWARE_DIR "reverse_engineering/firmware/"

/* One region of the EPROM as reverse_engineering/firmware/manifest.csv lays it
 * out: `header` holds the load address and the word count, or only the count
 * for the bootstrap the 80C32 streams to P:0 itself. */
typedef struct {
  const char *file;
  size_t header;
  size_t data;
  unsigned load_address;
  unsigned words;
  bool header_has_address;
} ifx_eprom_region;

static const ifx_eprom_region k_regions[] = {
    {"00_mcu_0000_19184b.bin", 0x0000, 0x0000, 0x0000, 0u, false},
    {"01_dsp_boot_p0000_211w.bin", 0x4AF0, 0x4AF2, 0x0000, 211u, false},
    {"02_dsp_shared_p0400_1552w.bin", 0x4D6B, 0x4D6F, 0x0400, 1552u, true},
    {"03_dsp_cfg2_p00a0_602w.bin", 0x5F9F, 0x5FA3, 0x00A0, 602u, true},
    {"04_dsp_cfg4_p00a0_363w.bin", 0x66B1, 0x66B5, 0x00A0, 363u, true},
    {"05_dsp_cfg5_p00a0_357w.bin", 0x6AF6, 0x6AFA, 0x00A0, 357u, true},
    {"06_dsp_cfg3_p00a0_263w.bin", 0x6F29, 0x6F2D, 0x00A0, 263u, true},
    {"07_dsp_cfg0_p00a0_534w.bin", 0x7242, 0x7246, 0x00A0, 534u, true},
    {"08_dsp_cfg1_p00a0_545w.bin", 0x7888, 0x788C, 0x00A0, 545u, true},
};

static uint8_t g_eprom[IFX_EPROM_SIZE];
static ifx_mcu_board *g_mcu;
static ifx_dsp_board *g_dsp;
static uint64_t g_clocks;
static uint32_t g_dsp_cycle_acc;
static uint32_t g_dsp_frame_acc;
static unsigned g_host_writes;
static unsigned g_host_reads;
static int16_t g_last_left;
static int16_t g_last_right;

static uint32_t dsp_cycles_for(uint64_t mcu_clocks) {
  /* 33 MHz of DSP clock over 12 MHz of MCU clock, two clocks per DSP
   * instruction cycle: 11/8 of a DSP cycle per MCU clock. */
  g_dsp_cycle_acc += (uint32_t)(mcu_clocks * 11u);
  uint32_t cycles = g_dsp_cycle_acc / 8u;
  g_dsp_cycle_acc -= cycles * 8u;
  return cycles;
}

static uint8_t host_read(void *user, unsigned reg) {
  (void)user;
  g_host_reads++;
  return ifx_dsp_board_host_read(g_dsp, reg);
}

static void host_write(void *user, unsigned reg, uint8_t value) {
  (void)user;
  g_host_writes++;
  printf("  host write reg=%u value=0x%02x at %llu us\n", reg, (unsigned)value,
         (unsigned long long)(g_clocks / IFX_MCU_BOARD_CLOCKS_PER_CYCLE));
  ifx_dsp_board_host_write(g_dsp, reg, value);
}

static void host_reset(void *user, bool asserted) {
  (void)user;
  printf("  dsp reset %s at %llu us\n", asserted ? "asserted" : "released",
         (unsigned long long)(g_clocks / IFX_MCU_BOARD_CLOCKS_PER_CYCLE));
  if (asserted) {
    ifx_dsp_board_reset(g_dsp);
  }
}

static void run_dsp(uint32_t cycles) {
  g_dsp_frame_acc += cycles;
  while (g_dsp_frame_acc >= IFX_DSP_BOARD_FRAME_CYCLES) {
    ifx_dsp_board_frame(g_dsp, 0, &g_last_left, &g_last_right);
    g_dsp_frame_acc -= IFX_DSP_BOARD_FRAME_CYCLES;
  }
}

static void run_clocks(uint64_t clocks) {
  uint64_t done = 0;
  while (done < clocks) {
    uint64_t used = ifx_mcu_board_step(g_mcu, IFX_MCU_BOARD_CLOCKS_PER_CYCLE);
    if (used == 0) {
      printf("  mcu halted after %llu clocks\n", (unsigned long long)g_clocks);
      break;
    }
    done += used;
    g_clocks += used;
    uint32_t cycles = dsp_cycles_for(used);
    if (cycles != 0u) {
      run_dsp(cycles);
    }
  }
}

static void run_ms(unsigned ms) { run_clocks((uint64_t)ms * (IFX_MCU_BOARD_XTAL_HZ / 1000u)); }

static bool read_file(const char *path, uint8_t *buffer, size_t size) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "cannot open %s\n", path);
    return false;
  }
  size_t read = fread(buffer, 1u, size, file);
  fclose(file);
  if (read != size) {
    fprintf(stderr, "%s is %zu bytes short\n", path, size - read);
    return false;
  }
  return true;
}

static bool build_eprom(void) {
  memset(g_eprom, 0xFF, sizeof(g_eprom));
  for (size_t i = 0; i < sizeof(k_regions) / sizeof(k_regions[0]); ++i) {
    const ifx_eprom_region *region = &k_regions[i];
    char path[128];
    snprintf(path, sizeof(path), "%s%s", IFX_FIRMWARE_DIR, region->file);
    size_t size = region->words != 0u ? (size_t)region->words * 3u : 19184u;
    if (region->data + size > sizeof(g_eprom)) {
      fprintf(stderr, "%s does not fit the EPROM\n", region->file);
      return false;
    }
    if (!read_file(path, &g_eprom[region->data], size)) {
      return false;
    }
    if (region->header == region->data) {
      continue;
    }
    if (region->header_has_address) {
      g_eprom[region->header] = (uint8_t)(region->load_address >> 8);
      g_eprom[region->header + 1u] = (uint8_t)region->load_address;
      g_eprom[region->header + 2u] = (uint8_t)(region->words >> 8);
      g_eprom[region->header + 3u] = (uint8_t)region->words;
    } else {
      g_eprom[region->header] = (uint8_t)(region->words >> 8);
      g_eprom[region->header + 1u] = (uint8_t)region->words;
    }
  }
  return true;
}

static void show(void) {
  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  ifx_mcu_board_display(g_mcu, text);
  printf("[%llu us] display \"%s\" pc=%04x leds=%02x seg=%02x%02x digit=%02x%02x\n",
         (unsigned long long)(g_clocks / 12u), text, (unsigned)ifx_mcu_board_pc(g_mcu),
         (unsigned)ifx_mcu_board_leds(g_mcu), (unsigned)ifx_mcu_board_xdata(g_mcu, 0x8005u),
         (unsigned)ifx_mcu_board_xdata(g_mcu, 0x8007u),
         (unsigned)ifx_mcu_board_xdata(g_mcu, 0x8006u),
         (unsigned)ifx_mcu_board_xdata(g_mcu, 0x8004u));
  printf("  preset=%u page=%02x function=%02x param=%02x store=%02x flags2e=%02x flags2f=%02x\n",
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x33u), (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Bu),
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x3Cu), (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Du),
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x3Fu), (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Eu),
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Fu));
  printf("  mode=%02x status=%02x state=%02x debounce=%02x index=%02x page2=%02x\n",
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x30u), (unsigned)ifx_mcu_board_iram(g_mcu, 0x42u),
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x36u), (unsigned)ifx_mcu_board_iram(g_mcu, 0x39u),
         (unsigned)ifx_mcu_board_iram(g_mcu, 0x3Du), (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Au));
  printf(
      "  settings %02x %02x %02x %02x %02x %02x  unmapped r=%u w=%u  host w=%u r=%u  "
      "dsp %s frames=%llu\n",
      (unsigned)ifx_mcu_board_xdata(g_mcu, 0x1BE9u), (unsigned)ifx_mcu_board_xdata(g_mcu, 0x1BEAu),
      (unsigned)ifx_mcu_board_xdata(g_mcu, 0x1BEBu), (unsigned)ifx_mcu_board_xdata(g_mcu, 0x1BECu),
      (unsigned)ifx_mcu_board_xdata(g_mcu, 0x1BEDu), (unsigned)ifx_mcu_board_xdata(g_mcu, 0x1BEEu),
      ifx_mcu_board_unmapped_reads(g_mcu), ifx_mcu_board_unmapped_writes(g_mcu), g_host_writes,
      g_host_reads, ifx_dsp_board_running(g_dsp) ? "running" : "halted",
      (unsigned long long)ifx_dsp_board_frames(g_dsp));
  printf("  audio l=%d r=%d\n", (int)g_last_left, (int)g_last_right);
  printf("  dsp cvr=%02x isr=%02x hsr=%02x hcr=%02x sr=%03x r0=%06x r6=%06x\n",
         (unsigned)ifx_dsp_board_host_read(g_dsp, 1), (unsigned)ifx_dsp_board_host_read(g_dsp, 2),
         (unsigned)ifx_dsp_board_word(g_dsp, DSP_SPACE_X, 0xFFC1),
         (unsigned)ifx_dsp_board_word(g_dsp, DSP_SPACE_X, 0xFFE8),
         (unsigned)ifx_dsp_board_reg(g_dsp, DSP_REG_SR),
         (unsigned)ifx_dsp_board_reg(g_dsp, DSP_REG_R0),
         (unsigned)ifx_dsp_board_reg(g_dsp, DSP_REG_R6));
}

static bool encoder_named(const char *name, ifx_mcu_board_encoder *encoder) {
  static const char *const names[IFX_MCU_BOARD_ENCODER_COUNT] = {"preset", "function", "parameter",
                                                                 "adjust"};
  for (unsigned i = 0; i < (unsigned)IFX_MCU_BOARD_ENCODER_COUNT; ++i) {
    if (strcmp(name, names[i]) == 0) {
      *encoder = (ifx_mcu_board_encoder)i;
      return true;
    }
  }
  return false;
}

static bool button_named(const char *name, ifx_mcu_board_button *button) {
  static const char *const names[IFX_MCU_BOARD_BUTTON_COUNT] = {"recall", "store", "compare",
                                                                "config", "bypass"};
  for (unsigned i = 0; i < (unsigned)IFX_MCU_BOARD_BUTTON_COUNT; ++i) {
    if (strcmp(name, names[i]) == 0) {
      *button = (ifx_mcu_board_button)i;
      return true;
    }
  }
  return false;
}

static void press(ifx_mcu_board_button button) {
  ifx_mcu_board_panel_button(g_mcu, button, true);
  run_ms(2);
}

static void release(ifx_mcu_board_button button) {
  ifx_mcu_board_panel_button(g_mcu, button, false);
  run_ms(2);
}

static unsigned last_pc;

static bool command(const char *arg) {
  if (arg[0] >= '0' && arg[0] <= '9') {
    run_ms((unsigned)strtoul(arg, NULL, 10));
    return true;
  }
  if (strcmp(arg, "show") == 0) {
    show();
    return true;
  }
  if (strncmp(arg, "turn:", 5) == 0 || strncmp(arg, "button:", 7) == 0 ||
      strncmp(arg, "press:", 6) == 0 || strncmp(arg, "release:", 8) == 0) {
    char name[16];
    const char *rest = strchr(arg, ':');
    rest = rest != NULL ? rest + 1 : arg;
    const char *colon = strchr(rest, ':');
    size_t length = colon != NULL ? (size_t)(colon - rest) : strlen(rest);
    if (length >= sizeof(name)) {
      return false;
    }
    memcpy(name, rest, length);
    name[length] = '\0';

    if (arg[0] == 't') {
      ifx_mcu_board_encoder encoder = IFX_MCU_BOARD_PRESET;
      if (!encoder_named(name, &encoder)) {
        return false;
      }
      int steps = colon != NULL ? (int)strtol(colon + 1, NULL, 10) : 1;
      int direction = steps < 0 ? -1 : 1;
      for (int i = 0; i < (steps < 0 ? -steps : steps); ++i) {
        ifx_mcu_board_panel_turn(g_mcu, encoder, direction);
        run_ms(5);
      }
      return true;
    }

    ifx_mcu_board_button button = IFX_MCU_BOARD_BUTTON_RECALL;
    if (!button_named(name, &button)) {
      return false;
    }
    if (arg[0] == 'b') {
      press(button);
      run_ms(30);
      release(button);
      run_ms(50);
    } else if (arg[0] == 'p') {
      press(button);
    } else {
      release(button);
    }
    return true;
  }
  if (strncmp(arg, "step:", 5) == 0) {
    unsigned steps = (unsigned)strtoul(arg + 5, NULL, 10);
    for (unsigned i = 0; i < steps; ++i) {
      ifx_mcu_board_step(g_mcu, IFX_MCU_BOARD_CLOCKS_PER_CYCLE);
      unsigned pc = (unsigned)ifx_mcu_board_pc(g_mcu);
      if (pc != last_pc) {
        last_pc = pc;
        printf(
            "  pc=%04x 2a=%02x 2b=%02x 2d=%02x 2e=%02x 2f=%02x 30=%02x 3c=%02x 3d=%02x\n", pc,
            (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Au), (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Bu),
            (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Du), (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Eu),
            (unsigned)ifx_mcu_board_iram(g_mcu, 0x2Fu), (unsigned)ifx_mcu_board_iram(g_mcu, 0x30u),
            (unsigned)ifx_mcu_board_iram(g_mcu, 0x3Cu), (unsigned)ifx_mcu_board_iram(g_mcu, 0x3Du));
      }
    }
    return true;
  }
  if (strcmp(arg, "dump") == 0) {
    for (unsigned base = 0x60u; base < 0x80u; base += 8u) {
      printf("  iram %02x:", base);
      for (unsigned i = 0; i < 8u; ++i) {
        printf(" %02x", (unsigned)ifx_mcu_board_iram(g_mcu, (uint8_t)(base + i)));
      }
      printf("\n");
    }
    return true;
  }
  if (strcmp(arg, "factory") == 0) {
    ifx_mcu_board_battery_factory(g_mcu);
    printf("  battery restored from the EPROM tables\n");
    return true;
  }
  if (strcmp(arg, "erase") == 0) {
    ifx_mcu_board_battery_erase(g_mcu);
    printf("  battery erased\n");
    return true;
  }
  if (strncmp(arg, "save:", 5) == 0) {
    bool ok = ifx_mcu_board_battery_save(g_mcu, arg + 5);
    printf("  battery saved to %s: %s\n", arg + 5, ok ? "ok" : "failed");
    return ok;
  }
  if (strncmp(arg, "load:", 5) == 0) {
    bool ok = ifx_mcu_board_battery_load(g_mcu, arg + 5);
    printf("  battery loaded from %s: %s\n", arg + 5, ok ? "ok" : "failed");
    return ok;
  }
  return false;
}

int main(int argc, char **argv) {
  g_dsp = ifx_dsp_board_create();
  if (g_dsp == NULL) {
    fprintf(stderr, "cannot create the DSP board\n");
    return 1;
  }
  g_mcu = ifx_mcu_board_create();
  if (g_mcu == NULL) {
    fprintf(stderr, "cannot create the MCU board\n");
    return 1;
  }
  if (!build_eprom() || !ifx_mcu_board_load_firmware(g_mcu, g_eprom, sizeof(g_eprom))) {
    return 1;
  }
  static const ifx_mcu_board_host_t host = {NULL, host_read, host_write, host_reset};
  ifx_mcu_board_set_host(g_mcu, &host);
  ifx_mcu_board_reset(g_mcu);

  for (int i = 1; i < argc; ++i) {
    if (!command(argv[i])) {
      fprintf(stderr, "unknown command %s\n", argv[i]);
      return 1;
    }
  }
  if (argc == 1) {
    run_ms(200);
    show();
  }
  ifx_mcu_board_destroy(g_mcu);
  ifx_dsp_board_destroy(g_dsp);
  return 0;
}
