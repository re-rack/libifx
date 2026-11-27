#include "ifx_emu.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_cpu.h"

/* The EPROM's SHA-256, as reverse_engineering/README.md records it. */
#define IFX_EMU_EPROM_SHA256 "7f6162d9e77e9b63783df7a8158c646d287e746de1ca56bdbea63b92e60004b1"

/* Panel registers the emu reads back; mcu.asm names them in its page scan. */
#define IFX_EMU_IRAM_MODE 0x30u      /* page list: 8 + configuration when bypassed */
#define IFX_EMU_IRAM_PRESET 0x33u    /* preset - 1 */
#define IFX_EMU_IRAM_PAGE 0x2Bu      /* the function page */
#define IFX_EMU_IRAM_PARAM 0x2Du     /* the parameter the page is showing */
#define IFX_EMU_IRAM_VALUES 0x80u    /* one byte per parameter, 0..107 */

/* mcu.asm C:1710: 18 bytes of page list per entry at 0x30's low nibble, the
 * eight plain configurations followed by the eight bypassed ones, then 15 bytes
 * of parameter list per page and 9 per special page (page & 0x0f). */
#define IFX_EMU_PAGE_LISTS 0x368Fu
#define IFX_EMU_PAGE_LIST_STRIDE 18u
#define IFX_EMU_PAGE_PARAMS 0x37AFu
#define IFX_EMU_PAGE_PARAM_STRIDE 15u
#define IFX_EMU_SPECIAL_PARAMS 0x3944u
#define IFX_EMU_SPECIAL_PARAM_STRIDE 9u

/* The DSP's parameter ring: x:$000f is the reader, x:$0010 the writer, and both
 * walk a 128-word window (M5 = $7f). */
#define IFX_EMU_RING_READ 0x000Fu
#define IFX_EMU_RING_WRITE 0x0010u
#define IFX_EMU_RING_BASE 0x0F80u
#define IFX_EMU_RING_SIZE 128u

#define IFX_EMU_MESSAGES 256u

/* C:0140 is the idle poll at the top of the main loop; it owns no host
 * transaction. */
#define IFX_EMU_MCU_IDLE_PC 0x0140u
#define IFX_EMU_MCU_CALL_SENTINEL 0xFFFFu

#define IFX_EMU_USER_PRESETS 0x0A81u
#define IFX_EMU_FACTORY_PRESETS 0x2084u
#define IFX_EMU_PRESET_BYTES 55u
#define IFX_EMU_LOAD_PRESET 2u
#define IFX_EMU_SETTLE_BLOCKS 4u
#define IFX_EMU_SETTLE_LIMIT 1000000u

/* POT3's quadrature gives up after this many transitions without the firmware
 * taking them, and each detent is one transition in the scan. */
#define IFX_EMU_TURN_MS 5u
#define IFX_EMU_PAGE_TURNS 24u

/* The panel's FACTORY RESTORE: preset 160's ENTER CODE parameter, and the code
 * that unlocks it. mcu.asm C:0312 lists the page, C:14E3's siblings the rest. */
#define IFX_EMU_FACTORY_PAGE 0x8Bu
#define IFX_EMU_ENTER_CODE_INDEX 0x27u
#define IFX_EMU_ENTER_CODE_VALUE 249u
#define IFX_EMU_RESTORE_MS 120000u

struct ifx_emu {
  ifx_mcu_board *mcu;
  ifx_dsp_board *dsp;
  uint8_t eprom[IFX_EMU_EPROM_SIZE];
  char eprom_path[4096];

  uint64_t clocks;
  uint32_t dsp_cycle_acc;
  uint32_t dsp_frame_acc;
  unsigned host_writes;
  unsigned host_reads;

  /* Audio: the feed the frame callback pulls from, and where its output goes. */
  ifx_emu_feed feed;
  size_t feed_index;
  int32_t *out_left;
  int32_t *out_right;
  uint64_t frame_limit;
  bool feeding;

  /* Ring observation, one sample per audio frame. */
  uint32_t ring_read;
  uint32_t ring_write;
  bool ring_valid;
  bool start_pending;
  ifx_emu_message messages[IFX_EMU_MESSAGES];
  unsigned message_count;
};

/* SHA-256, FIPS 180-4. Only the EPROM's digest is ever computed. */
typedef struct {
  uint32_t state[8];
  uint64_t length;
  uint8_t block[64];
  size_t used;
} ifx_emu_sha256;

static const uint32_t k_sha256_k[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u,
    0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu,
    0x9BDC06A7u, 0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu,
    0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu, 0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu,
    0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u, 0xA2BFE8A1u, 0xA81A664Bu,
    0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u, 0x19A4C116u,
    0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u,
    0xC67178F2u};

static uint32_t sha256_rotr(uint32_t value, unsigned count) {
  return (value >> count) | (value << (32u - count));
}

static void sha256_compress(ifx_emu_sha256 *ctx, const uint8_t *block) {
  uint32_t w[64];
  for (unsigned i = 0; i < 16u; ++i) {
    w[i] = ((uint32_t)block[i * 4u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16) |
           ((uint32_t)block[i * 4u + 2u] << 8) | (uint32_t)block[i * 4u + 3u];
  }
  for (unsigned i = 16; i < 64u; ++i) {
    uint32_t s0 = sha256_rotr(w[i - 15u], 7u) ^ sha256_rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
    uint32_t s1 = sha256_rotr(w[i - 2u], 17u) ^ sha256_rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];
  uint32_t f = ctx->state[5];
  uint32_t g = ctx->state[6];
  uint32_t h = ctx->state[7];
  for (unsigned i = 0; i < 64u; ++i) {
    uint32_t s1 = sha256_rotr(e, 6u) ^ sha256_rotr(e, 11u) ^ sha256_rotr(e, 25u);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + s1 + ch + k_sha256_k[i] + w[i];
    uint32_t s0 = sha256_rotr(a, 2u) ^ sha256_rotr(a, 13u) ^ sha256_rotr(a, 22u);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void sha256_init(ifx_emu_sha256 *ctx) {
  static const uint32_t seed[8] = {0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
                                   0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u};
  memcpy(ctx->state, seed, sizeof(seed));
  ctx->length = 0;
  ctx->used = 0;
}

static void sha256_update(ifx_emu_sha256 *ctx, const uint8_t *data, size_t size) {
  ctx->length += size;
  while (size > 0u) {
    size_t room = sizeof(ctx->block) - ctx->used;
    size_t take = size < room ? size : room;
    memcpy(&ctx->block[ctx->used], data, take);
    ctx->used += take;
    data += take;
    size -= take;
    if (ctx->used == sizeof(ctx->block)) {
      sha256_compress(ctx, ctx->block);
      ctx->used = 0;
    }
  }
}

static void sha256_hex(ifx_emu_sha256 *ctx, char *hex) {
  uint64_t bits = ctx->length * 8u;
  uint8_t pad[72];
  size_t pad_size = ctx->used < 56u ? 56u - ctx->used : 120u - ctx->used;
  memset(pad, 0, sizeof(pad));
  pad[0] = 0x80u;
  for (unsigned i = 0; i < 8u; ++i) {
    pad[pad_size + i] = (uint8_t)(bits >> (56u - i * 8u));
  }
  sha256_update(ctx, pad, pad_size + 8u);
  static const char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < 8u; ++i) {
    for (unsigned byte = 0; byte < 4u; ++byte) {
      uint8_t value = (uint8_t)(ctx->state[i] >> (24u - byte * 8u));
      hex[i * 8u + byte * 2u] = digits[value >> 4];
      hex[i * 8u + byte * 2u + 1u] = digits[value & 0x0Fu];
    }
  }
  hex[64] = '\0';
}

static bool file_digest_matches(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return false;
  }
  ifx_emu_sha256 ctx;
  sha256_init(&ctx);
  uint8_t buffer[4096];
  for (;;) {
    size_t read = fread(buffer, 1u, sizeof(buffer), file);
    sha256_update(&ctx, buffer, read);
    if (read < sizeof(buffer)) {
      break;
    }
  }
  bool ok = ferror(file) == 0;
  fclose(file);
  if (!ok) {
    return false;
  }
  char hex[65];
  sha256_hex(&ctx, hex);
  return strcmp(hex, IFX_EMU_EPROM_SHA256) == 0;
}

/* The digests live in the project root and in reverse_engineering/inputs; any
 * other file under either is a candidate the digest rejects. */
static bool find_in_directory(const char *directory, char *path, size_t size) {
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    return false;
  }
  bool found = false;
  const struct dirent *entry = NULL;
  while (!found && (entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    int written = snprintf(path, size, "%s/%s", directory, entry->d_name);
    if (written > 0 && (size_t)written < size) {
      found = file_digest_matches(path);
    }
  }
  closedir(dir);
  return found;
}

static bool find_eprom(const char *root, char *path, size_t size) {
  char inputs[4096];
  if (root != NULL && root[0] != '\0' && file_digest_matches(root)) {
    int written = snprintf(path, size, "%s", root);
    return written > 0 && (size_t)written < size;
  }
  const char *base = root != NULL && root[0] != '\0' ? root : ".";
  int written = snprintf(inputs, sizeof(inputs), "%s/reverse_engineering/inputs", base);
  if (written > 0 && (size_t)written < (size_t)sizeof(inputs) &&
      find_in_directory(inputs, path, size)) {
    return true;
  }
  return find_in_directory(base, path, size);
}

static uint32_t dsp_cycles_for(ifx_emu *emu, uint64_t mcu_clocks) {
  /* 33 MHz of DSP clock over 12 MHz of MCU clock: 11/4 of a DSP clock cycle,
   * the core's unit, per MCU clock. */
  emu->dsp_cycle_acc += (uint32_t)(mcu_clocks * 11u);
  uint32_t cycles = emu->dsp_cycle_acc / 4u;
  emu->dsp_cycle_acc -= cycles * 4u;
  return cycles;
}

static uint32_t ring_address(uint32_t base, unsigned offset) {
  return IFX_EMU_RING_BASE + ((base - IFX_EMU_RING_BASE + offset) % IFX_EMU_RING_SIZE);
}

static void message_append(ifx_emu *emu, uint64_t frame, uint32_t read, uint32_t write) {
  if (read < IFX_EMU_RING_BASE || read >= IFX_EMU_RING_BASE + IFX_EMU_RING_SIZE ||
      write < IFX_EMU_RING_BASE || write >= IFX_EMU_RING_BASE + IFX_EMU_RING_SIZE) {
    return;
  }
  unsigned wrote = (write - read) % IFX_EMU_RING_SIZE;
  for (unsigned offset = 0; offset + 1u < wrote; offset += 2u) {
    if (emu->message_count == IFX_EMU_MESSAGES) {
      memmove(&emu->messages[0], &emu->messages[1],
              sizeof(emu->messages[0]) * (IFX_EMU_MESSAGES - 1u));
      emu->message_count--;
    }
    ifx_emu_message *message = &emu->messages[emu->message_count++];
    message->frame_in = frame;
    message->frame_applied = 0;
    message->pass_applied = 0;
    message->index = ifx_dsp_board_word(emu->dsp, DSP_SPACE_Y, ring_address(read, offset));
    message->value = ifx_dsp_board_word(emu->dsp, DSP_SPACE_Y, ring_address(read, offset + 1u));
  }
}

static void observe_ring(ifx_emu *emu) {
  uint32_t read = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, IFX_EMU_RING_READ);
  uint32_t write = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, IFX_EMU_RING_WRITE);
  if (!emu->ring_valid) {
    emu->ring_valid = true;
    emu->ring_read = read;
    emu->ring_write = write;
    return;
  }
  uint64_t frame = ifx_dsp_board_frames(emu->dsp);
  uint64_t pass = ifx_dsp_board_passes(emu->dsp);
  unsigned read_out = (read - emu->ring_read) % IFX_EMU_RING_SIZE;
  uint32_t old_write = emu->ring_write;
  emu->ring_read = read;
  emu->ring_write = write;

  message_append(emu, frame, old_write, write);
  if (read_out != 0u) {
    /* A block drains the ring whole, so every waiting message is applied in
     * this frame. */
    for (unsigned i = 0; i < emu->message_count; ++i) {
      if (emu->messages[i].frame_applied == 0u) {
        emu->messages[i].frame_applied = frame;
        emu->messages[i].pass_applied = pass;
      }
    }
  }
}

static void frame(ifx_emu *emu) {
  uint32_t in = 0;
  if (emu->feed_index < emu->feed.frames) {
    int32_t sample = emu->feed.samples != NULL ? emu->feed.samples[emu->feed_index] : 0;
    in = emu->feed.adc16 ? ((uint32_t)sample >> 8) & 0xFFFFu : (uint32_t)sample & 0xFFFFFFu;
  }
  size_t index = emu->feed_index;
  emu->feed_index++;
  int32_t left = 0;
  int32_t right = 0;
  if (emu->feed.adc16) {
    ifx_dsp_board_frame(emu->dsp, in, &left, &right);
  } else {
    ifx_dsp_board_frame_wide(emu->dsp, in, &left, &right);
  }
  if (emu->out_left != NULL) {
    emu->out_left[index] = left;
  }
  if (emu->out_right != NULL) {
    emu->out_right[index] = right;
  }
  observe_ring(emu);
}

static void run_dsp(ifx_emu *emu, uint32_t cycles) {
  emu->dsp_frame_acc += cycles;
  while (emu->dsp_frame_acc >= IFX_DSP_BOARD_FRAME_CYCLES) {
    frame(emu);
    emu->dsp_frame_acc -= IFX_DSP_BOARD_FRAME_CYCLES;
  }
}

static void run_clocks(ifx_emu *emu, uint64_t clocks) {
  uint64_t done = 0;
  while (done < clocks) {
    if (emu->feeding && ifx_dsp_board_frames(emu->dsp) >= emu->frame_limit) {
      return;
    }
    uint64_t used = ifx_mcu_board_step(emu->mcu, IFX_MCU_BOARD_CLOCKS_PER_CYCLE);
    if (used == 0u) {
      return;
    }
    done += used;
    emu->clocks += used;
    uint32_t cycles = dsp_cycles_for(emu, used);
    if (cycles != 0u) {
      run_dsp(emu, cycles);
    }
  }
}

static uint8_t host_read(void *user, unsigned reg) {
  ifx_emu *emu = user;
  emu->host_reads++;
  return ifx_dsp_board_host_read(emu->dsp, reg);
}

static void host_write(void *user, unsigned reg, uint8_t value) {
  ifx_emu *emu = user;
  emu->host_writes++;
  ifx_dsp_board_host_write(emu->dsp, reg, value);
}

static void host_reset(void *user, bool asserted) {
  if (asserted) {
    ifx_emu *emu = user;
    ifx_dsp_board_reset(emu->dsp);
    emu->dsp_cycle_acc = 0;
    emu->dsp_frame_acc = 0;
    emu->ring_valid = false;
  }
}

void ifx_emu_run_ms(ifx_emu *emu, unsigned ms) {
  run_clocks(emu, (uint64_t)ms * (IFX_MCU_BOARD_XTAL_HZ / 1000u));
}

static bool ifx_emu_ready(const ifx_emu *emu);

/* The DSP starts 2.27 s into the boot and needs its first block before the
 * panel answers, so the boot is over a second after the core is running. */
static bool boot(ifx_emu *emu) {
  ifx_mcu_board_reset(emu->mcu);
  for (unsigned waited = 0; waited < 8000u; waited += 100u) {
    if (ifx_dsp_board_running(emu->dsp)) {
      break;
    }
    ifx_emu_run_ms(emu, 100);
  }
  ifx_emu_run_ms(emu, 1000);
  return ifx_emu_ready(emu);
}

ifx_emu *ifx_emu_create(const char *root) {
  ifx_emu *emu = calloc(1, sizeof(*emu));
  if (emu == NULL) {
    return NULL;
  }
  if (!find_eprom(root, emu->eprom_path, sizeof(emu->eprom_path))) {
    fprintf(stderr, "no file under %s matches the EPROM's SHA-256\n", root != NULL ? root : ".");
    free(emu);
    return NULL;
  }
  FILE *file = fopen(emu->eprom_path, "rb");
  if (file == NULL) {
    fprintf(stderr, "cannot open %s\n", emu->eprom_path);
    free(emu);
    return NULL;
  }
  size_t read = fread(emu->eprom, 1u, sizeof(emu->eprom), file);
  fclose(file);
  if (read != sizeof(emu->eprom)) {
    fprintf(stderr, "%s is %zu bytes, not %u\n", emu->eprom_path, read,
            (unsigned)IFX_EMU_EPROM_SIZE);
    free(emu);
    return NULL;
  }

  emu->dsp = ifx_dsp_board_create();
  emu->mcu = ifx_mcu_board_create();
  if (emu->dsp == NULL || emu->mcu == NULL) {
    fprintf(stderr, "cannot create the boards\n");
    ifx_emu_destroy(emu);
    return NULL;
  }
  if (!ifx_mcu_board_load_firmware(emu->mcu, emu->eprom, sizeof(emu->eprom))) {
    fprintf(stderr, "the firmware does not fit the MCU's code space\n");
    ifx_emu_destroy(emu);
    return NULL;
  }
  ifx_mcu_board_host_t host = {emu, host_read, host_write, host_reset};
  ifx_mcu_board_set_host(emu->mcu, &host);
  ifx_mcu_board_battery_factory(emu->mcu);
  if (!boot(emu)) {
    fprintf(stderr, "the DSP did not start\n");
    ifx_emu_destroy(emu);
    return NULL;
  }
  return emu;
}

void ifx_emu_destroy(ifx_emu *emu) {
  if (emu == NULL) {
    return;
  }
  ifx_mcu_board_destroy(emu->mcu);
  ifx_dsp_board_destroy(emu->dsp);
  free(emu);
}

ifx_mcu_board *ifx_emu_mcu(const ifx_emu *emu) { return emu->mcu; }

ifx_dsp_board *ifx_emu_dsp(const ifx_emu *emu) { return emu->dsp; }

const char *ifx_emu_eprom_path(const ifx_emu *emu) { return emu->eprom_path; }

const uint8_t *ifx_emu_eprom(const ifx_emu *emu) { return emu->eprom; }

bool ifx_emu_factory_preset(const ifx_emu *emu, unsigned preset, unsigned *config,
                            uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  if (emu == NULL || config == NULL || values == NULL || preset < 81u ||
      preset > IFX_EMU_PRESET_COUNT) {
    return false;
  }
  const uint8_t *record =
      &emu->eprom[IFX_EMU_FACTORY_PRESETS + IFX_EMU_PRESET_BYTES * (preset - 81u)];
  unsigned cfg = record[0] & 0x07u;
  if (cfg >= 6u) {
    cfg = 0u;
  }
  *config = cfg;
  memset(values, 0, IFX_EMU_PARAMETER_COUNT);
  const uint8_t *list = &emu->eprom[IFX_EMU_PARAMETER_LISTS + IFX_EMU_PARAMETER_LIST_STRIDE * cfg];
  unsigned position = 1u;
  for (unsigned i = 0; i < IFX_EMU_PARAMETER_LIST_STRIDE && list[i] != 0u; ++i) {
    unsigned index = list[i];
    if (index >= IFX_EMU_PARAMETER_COUNT || position >= IFX_EMU_PRESET_BYTES) {
      return false;
    }
    values[index] = record[position++];
  }
  return true;
}

static bool ifx_emu_ready(const ifx_emu *emu) {
  return ifx_dsp_board_running(emu->dsp) && ifx_dsp_board_frames(emu->dsp) != 0u;
}

uint64_t ifx_emu_frames(const ifx_emu *emu) { return ifx_dsp_board_frames(emu->dsp); }

uint64_t ifx_emu_clocks(const ifx_emu *emu) { return emu->clocks; }

static uint64_t step_mcu(ifx_emu *emu) {
  uint64_t used = ifx_mcu_board_step(emu->mcu, IFX_MCU_BOARD_CLOCKS_PER_CYCLE);
  if (used == 0u) {
    return 0;
  }
  emu->clocks += used;
  uint32_t cycles = dsp_cycles_for(emu, used);
  if (cycles != 0u) {
    ifx_dsp_board_run(emu->dsp, cycles);
  }
  return used;
}

static bool mcu_wait_idle(ifx_emu *emu) {
  for (unsigned i = 0; i < 100000u; ++i) {
    if (ifx_mcu_board_pc(emu->mcu) == IFX_EMU_MCU_IDLE_PC && !ifx_mcu_board_in_isr(emu->mcu)) {
      return true;
    }
    if (step_mcu(emu) == 0u) {
      return false;
    }
  }
  return false;
}

bool ifx_emu_send(ifx_emu *emu, unsigned index, unsigned value) {
  if (emu == NULL || index >= IFX_EMU_PARAMETER_COUNT || !ifx_dsp_board_running(emu->dsp)) {
    return false;
  }
  unsigned maximum = emu->eprom[IFX_EMU_MAX_TABLE + index];
  if (maximum == 0u || value >= maximum) {
    return false;
  }
  if (!mcu_wait_idle(emu)) {
    return false;
  }

  ifx_mcu_board_cpu_state saved;
  ifx_mcu_board_cpu_save(emu->mcu, &saved);
  if (saved.sp > 0xFDu) {
    fprintf(stderr, "ifx_emu_send: SP %02x leaves no room for the call's return address\n",
            (unsigned)saved.sp);
    return false;
  }
  observe_ring(emu);
  uint32_t start_write = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, IFX_EMU_RING_WRITE);

  ifx_mcu_board_set_iram(emu->mcu, (uint8_t)(saved.sp + 1u),
                         (uint8_t)(IFX_EMU_MCU_CALL_SENTINEL & 0xFFu));
  ifx_mcu_board_set_iram(emu->mcu, (uint8_t)(saved.sp + 2u),
                         (uint8_t)(IFX_EMU_MCU_CALL_SENTINEL >> 8));
  ifx_mcu_board_set_iram(emu->mcu, (uint8_t)(IFX_EMU_IRAM_VALUES + index), (uint8_t)value);

  ifx_mcu_board_cpu_state call = saved;
  call.pc = 0x0EE0u;
  call.sp = (uint8_t)(saved.sp + 2u);
  call.regs[1] = (uint8_t)(IFX_EMU_IRAM_VALUES + index);
  call.regs[2] = (uint8_t)index;
  ifx_mcu_board_cpu_restore(emu->mcu, &call);

  bool returned = false;
  for (unsigned i = 0; i < 200000u; ++i) {
    if (ifx_mcu_board_pc(emu->mcu) == IFX_EMU_MCU_CALL_SENTINEL) {
      returned = true;
      break;
    }
    if (step_mcu(emu) == 0u) {
      break;
    }
  }
  ifx_mcu_board_cpu_restore(emu->mcu, &saved);
  if (!returned) {
    return false;
  }

  for (unsigned i = 0; i < 200000u; ++i) {
    uint32_t write = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, IFX_EMU_RING_WRITE);
    if ((write - start_write) % IFX_EMU_RING_SIZE >= 2u) {
      break;
    }
    ifx_dsp_board_run(emu->dsp, 1u);
  }
  uint32_t write = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, IFX_EMU_RING_WRITE);
  if ((write - start_write) % IFX_EMU_RING_SIZE < 2u) {
    return false;
  }
  observe_ring(emu);
  return step_mcu(emu) != 0u;
}

unsigned ifx_emu_block_phase(const ifx_emu *emu) {
  uint32_t pointer = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0001u);
  return (pointer - 0x80u) & 0x1Fu;
}

bool ifx_emu_apply(ifx_emu *emu) {
  if (emu == NULL || !ifx_dsp_board_running(emu->dsp)) {
    return false;
  }
  if (emu->start_pending) {
    emu->start_pending = false;
    for (unsigned i = 0; i < 64u; ++i) {
      ifx_emu_feed feed = {NULL, 1u, false};
      if (!ifx_emu_audio(emu, &feed, NULL, NULL)) {
        return false;
      }
      if ((ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0000u) & (1u << 3)) == 0u) {
        break;
      }
    }
    if ((ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0000u) & (1u << 3)) != 0u) {
      return false;
    }
  }
  if ((ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0000u) & (1u << 1)) == 0u) {
    return true;
  }
  ifx_dsp_board_set_breakpoint(emu->dsp, 0x04BCu);
  bool hit = false;
  for (unsigned i = 0; i < 64u && !hit; ++i) {
    ifx_emu_feed feed = {NULL, 1u, false};
    if (!ifx_emu_audio(emu, &feed, NULL, NULL)) {
      break;
    }
    hit = ifx_dsp_board_breakpoint_hit(emu->dsp);
  }
  ifx_dsp_board_clear_breakpoint(emu->dsp);
  return hit;
}

bool ifx_emu_align(ifx_emu *emu) {
  if (emu == NULL || !ifx_emu_ready(emu)) {
    return false;
  }
  for (unsigned i = 0; i < 64u && ifx_emu_block_phase(emu) != 0u; ++i) {
    ifx_emu_feed feed = {NULL, 1u, false};
    if (!ifx_emu_audio(emu, &feed, NULL, NULL)) {
      return false;
    }
  }
  return ifx_emu_block_phase(emu) == 0u;
}

uint64_t ifx_emu_blocks(const ifx_emu *emu) { return ifx_dsp_board_passes(emu->dsp); }

static const uint8_t *parameter_list(const ifx_emu *emu, unsigned config) {
  return &emu->eprom[IFX_EMU_PARAMETER_LISTS + IFX_EMU_PARAMETER_LIST_STRIDE * config];
}

static bool recall_to_init(ifx_emu *emu, unsigned preset) {
  if (preset < 1u || preset > IFX_EMU_PRESET_COUNT) {
    return false;
  }
  unsigned wanted = preset - 1u;
  unsigned current = ifx_mcu_board_iram(emu->mcu, IFX_EMU_IRAM_PRESET);
  if (current != wanted) {
    int delta = (int)wanted - (int)current;
    ifx_emu_turn(emu, IFX_MCU_BOARD_PRESET, delta);
  }
  uint32_t start_wipe = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0017u);
  ifx_emu_button(emu, IFX_MCU_BOARD_BUTTON_RECALL, true);
  const uint64_t release = ifx_emu_clocks(emu) + 30u * (IFX_MCU_BOARD_XTAL_HZ / 1000u);
  bool released = false;
  bool wiped = false;
  for (unsigned tick = 0; tick < IFX_EMU_SETTLE_LIMIT; ++tick) {
    uint32_t flags = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0000u);
    uint32_t wipe = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0017u);
    if ((flags & (1u << 11)) != 0u || wipe != start_wipe) {
      wiped = true;
    }
    /* The last wipe pass starts the block whose pass runs the initialisation. */
    if (wiped && wipe <= 0x1000u && ifx_emu_block_phase(emu) == 0u) {
      ifx_mcu_board_panel_button(emu->mcu, IFX_MCU_BOARD_BUTTON_RECALL, false);
      return ifx_mcu_board_iram(emu->mcu, IFX_EMU_IRAM_PRESET) == wanted;
    }
    if (!released && ifx_emu_clocks(emu) >= release) {
      ifx_mcu_board_panel_button(emu->mcu, IFX_MCU_BOARD_BUTTON_RECALL, false);
      released = true;
    }
    ifx_emu_feed feed = {NULL, 1u, false};
    if (!ifx_emu_audio(emu, &feed, NULL, NULL)) {
      return false;
    }
  }
  if (!released) {
    ifx_mcu_board_panel_button(emu->mcu, IFX_MCU_BOARD_BUTTON_RECALL, false);
  }
  return false;
}

static bool load_preset(ifx_emu *emu, unsigned config,
                        const uint8_t values[IFX_EMU_PARAMETER_COUNT], bool at_init) {
  if (emu == NULL || values == NULL || config >= 6u) {
    return false;
  }
  const uint8_t *list = parameter_list(emu, config);
  bool present[IFX_EMU_PARAMETER_COUNT] = {false};
  uint8_t preset[IFX_EMU_PRESET_BYTES] = {0};
  preset[0] = (uint8_t)config;
  unsigned position = 1u;
  for (unsigned i = 0; i < IFX_EMU_PARAMETER_LIST_STRIDE && list[i] != 0u; ++i) {
    unsigned index = list[i];
    if (index >= IFX_EMU_PARAMETER_COUNT || position >= IFX_EMU_PRESET_BYTES) {
      return false;
    }
    unsigned maximum = emu->eprom[IFX_EMU_MAX_TABLE + index];
    if (maximum == 0u || values[index] >= maximum) {
      return false;
    }
    present[index] = true;
    preset[position++] = values[index];
  }
  for (unsigned index = 0; index < IFX_EMU_PARAMETER_COUNT; ++index) {
    if (!present[index] && values[index] != 0u) {
      return false;
    }
  }

  uint8_t *battery = ifx_mcu_board_battery(emu->mcu);
  uint8_t *record =
      &battery[IFX_EMU_USER_PRESETS + IFX_EMU_PRESET_BYTES * (IFX_EMU_LOAD_PRESET - 1u)];
  memcpy(record, preset, sizeof(preset));
  /* A preset recall keeps the DSP's running state; a run must start from a
   * power-on state so the wipe and initialisation really run. */
  ifx_dsp_board_clear_memory(emu->dsp);
  ifx_emu_reset(emu);
  bool recalled =
      at_init ? recall_to_init(emu, IFX_EMU_LOAD_PRESET) : ifx_emu_recall(emu, IFX_EMU_LOAD_PRESET);
  if (!recalled || ifx_emu_config(emu) != config) {
    return false;
  }
  for (unsigned i = 0; i < IFX_EMU_PARAMETER_LIST_STRIDE && list[i] != 0u; ++i) {
    if (ifx_emu_value(emu, list[i]) != values[list[i]]) {
      return false;
    }
  }
  return true;
}

bool ifx_emu_load(ifx_emu *emu, unsigned config, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  return load_preset(emu, config, values, false);
}

bool ifx_emu_start(ifx_emu *emu, unsigned config, const uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  if (!load_preset(emu, config, values, true)) {
    return false;
  }
  emu->start_pending = true;
  return true;
}

bool ifx_emu_settle(ifx_emu *emu, uint64_t *frames_used) {
  if (emu == NULL || !ifx_dsp_board_running(emu->dsp)) {
    return false;
  }
  uint64_t start = ifx_dsp_board_frames(emu->dsp);
  unsigned quiet = 0;
  for (uint64_t i = 0; i < IFX_EMU_SETTLE_LIMIT; ++i) {
    int32_t left = 0;
    int32_t right = 0;
    ifx_emu_feed feed = {NULL, 1u, false};
    if (!ifx_emu_audio(emu, &feed, &left, &right)) {
      return false;
    }
    uint32_t flags = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0000u);
    uint32_t wipe = ifx_dsp_board_word(emu->dsp, DSP_SPACE_X, 0x0017u);
    bool settled = (flags & ((1u << 3) | (1u << 7) | (1u << 11))) == 0u && wipe <= 0x1000u;
    if (settled && left == IFX_EMU_REST_WORD && right == IFX_EMU_REST_WORD) {
      quiet++;
    } else {
      quiet = 0;
    }
    if (quiet >= IFX_EMU_SETTLE_BLOCKS * 32u) {
      if (frames_used != NULL) {
        *frames_used = ifx_dsp_board_frames(emu->dsp) - start;
      }
      return true;
    }
  }
  return false;
}

void ifx_emu_turn(ifx_emu *emu, ifx_mcu_board_encoder encoder, int detents) {
  int direction = detents < 0 ? -1 : 1;
  for (int i = 0; i < (detents < 0 ? -detents : detents); ++i) {
    ifx_mcu_board_panel_turn(emu->mcu, encoder, direction);
    ifx_emu_run_ms(emu, IFX_EMU_TURN_MS);
  }
}

void ifx_emu_button(ifx_emu *emu, ifx_mcu_board_button button, bool pressed) {
  ifx_mcu_board_panel_button(emu->mcu, button, pressed);
  ifx_emu_run_ms(emu, 2);
}

static unsigned iram(const ifx_emu *emu, uint8_t address) {
  return ifx_mcu_board_iram(emu->mcu, address);
}

unsigned ifx_emu_preset(const ifx_emu *emu) { return iram(emu, IFX_EMU_IRAM_PRESET) + 1u; }

unsigned ifx_emu_config(const ifx_emu *emu) { return iram(emu, IFX_EMU_IRAM_MODE) & 0x07u; }

unsigned ifx_emu_page(const ifx_emu *emu) { return iram(emu, IFX_EMU_IRAM_PAGE); }

unsigned ifx_emu_index(const ifx_emu *emu) { return iram(emu, IFX_EMU_IRAM_PARAM); }

unsigned ifx_emu_value(const ifx_emu *emu, unsigned index) {
  if (index >= IFX_EMU_PARAMETER_COUNT) {
    return 0;
  }
  return iram(emu, (uint8_t)(IFX_EMU_IRAM_VALUES + index));
}

void ifx_emu_display(const ifx_emu *emu, char *text) { ifx_mcu_board_display(emu->mcu, text); }

void ifx_emu_host_traffic(const ifx_emu *emu, unsigned *writes, unsigned *reads) {
  if (writes != NULL) {
    *writes = emu->host_writes;
  }
  if (reads != NULL) {
    *reads = emu->host_reads;
  }
}

bool ifx_emu_recall(ifx_emu *emu, unsigned preset) {
  if (preset < 1u || preset > IFX_EMU_PRESET_COUNT) {
    return false;
  }
  unsigned wanted = preset - 1u;
  unsigned current = iram(emu, IFX_EMU_IRAM_PRESET);
  if (current != wanted) {
    int delta = (int)wanted - (int)current;
    ifx_emu_turn(emu, IFX_MCU_BOARD_PRESET, delta);
  }
  ifx_emu_button(emu, IFX_MCU_BOARD_BUTTON_RECALL, true);
  ifx_emu_run_ms(emu, 30);
  ifx_emu_button(emu, IFX_MCU_BOARD_BUTTON_RECALL, false);
  ifx_emu_run_ms(emu, 300);
  return iram(emu, IFX_EMU_IRAM_PRESET) == wanted;
}

/* The page's parameter list, as the firmware's own scan indexes it. */
static const uint8_t *page_params(const ifx_emu *emu, unsigned page, unsigned *count) {
  if (page >= 0x80u) {
    if (page > 0x8Bu) {
      return NULL;
    }
    *count = IFX_EMU_SPECIAL_PARAM_STRIDE;
    return &emu->eprom[IFX_EMU_SPECIAL_PARAMS + IFX_EMU_SPECIAL_PARAM_STRIDE * (page & 0x0Fu)];
  }
  *count = IFX_EMU_PAGE_PARAM_STRIDE;
  return &emu->eprom[IFX_EMU_PAGE_PARAMS + IFX_EMU_PAGE_PARAM_STRIDE * page];
}

/* Position 0 is unreachable from the function knob, which clamps at 1, so the
 * search starts there and stops at the record's terminator. */
static unsigned page_position(const ifx_emu *emu, unsigned page, unsigned index) {
  unsigned count = 0;
  const uint8_t *record = page_params(emu, page, &count);
  if (record == NULL) {
    return 0;
  }
  for (unsigned position = 1; position < count; ++position) {
    if (record[position] == index) {
      return position;
    }
    if (record[position] == 0u) {
      return 0;
    }
  }
  return 0;
}

static const uint8_t *page_list(const ifx_emu *emu) {
  unsigned mode = iram(emu, IFX_EMU_IRAM_MODE) & 0x0Fu;
  return &emu->eprom[IFX_EMU_PAGE_LISTS + IFX_EMU_PAGE_LIST_STRIDE * mode];
}

/* Turns until the scan's own page register holds `page`. The caller walks the
 * list forward, so this only ever turns one way. */
static bool seek_page(ifx_emu *emu, unsigned page) {
  for (unsigned turns = 0; turns < IFX_EMU_PAGE_TURNS && ifx_emu_page(emu) != page; ++turns) {
    ifx_emu_turn(emu, IFX_MCU_BOARD_FUNCTION_SELECT, 1);
  }
  return ifx_emu_page(emu) == page;
}

static bool ifx_emu_select(ifx_emu *emu, unsigned index) {
  if (index >= IFX_EMU_PARAMETER_COUNT) {
    return false;
  }
  if (ifx_emu_index(emu) == index) {
    return true;
  }
  ifx_emu_turn(emu, IFX_MCU_BOARD_FUNCTION_SELECT, -(int)IFX_EMU_PAGE_TURNS);
  const uint8_t *list = page_list(emu);
  for (unsigned i = 1; i < IFX_EMU_PAGE_LIST_STRIDE && list[i] != 0u; ++i) {
    unsigned position = page_position(emu, list[i], index);
    if (position == 0u) {
      continue;
    }
    if (!seek_page(emu, list[i])) {
      return false;
    }
    ifx_emu_turn(emu, IFX_MCU_BOARD_PARAMETER_SELECT, (int)position - 1);
    return ifx_emu_index(emu) == index;
  }
  return false;
}

bool ifx_emu_set_parameter(ifx_emu *emu, unsigned index, unsigned value) {
  if (!ifx_emu_select(emu, index)) {
    return false;
  }
  for (unsigned step = 0; step < 2u * IFX_EMU_PARAMETER_COUNT; ++step) {
    unsigned current = ifx_emu_value(emu, index);
    if (current == value) {
      return true;
    }
    int direction = value > current ? 1 : -1;
    ifx_emu_turn(emu, IFX_MCU_BOARD_PARAMETER_ADJUST, direction);
    if (ifx_emu_value(emu, index) == current) {
      return false;
    }
  }
  return false;
}

bool ifx_emu_audio(ifx_emu *emu, const ifx_emu_feed *feed, int32_t *left, int32_t *right) {
  if (feed == NULL || feed->frames == 0u || !ifx_dsp_board_running(emu->dsp)) {
    return false;
  }
  emu->feed = *feed;
  emu->feed_index = 0;
  emu->out_left = left;
  emu->out_right = right;
  emu->frame_limit = ifx_dsp_board_frames(emu->dsp) + feed->frames;
  emu->feeding = true;
  /* One machine cycle per step, so the run stops on the frame the feed asks for
   * instead of overshooting it; a step that runs no clock means the MCU
   * stopped. */
  while (ifx_dsp_board_frames(emu->dsp) < emu->frame_limit) {
    uint64_t before = emu->clocks;
    run_clocks(emu, IFX_MCU_BOARD_CLOCKS_PER_CYCLE);
    if (emu->clocks == before) {
      break;
    }
  }
  bool complete = ifx_dsp_board_frames(emu->dsp) >= emu->frame_limit;
  emu->feeding = false;
  emu->out_left = NULL;
  emu->out_right = NULL;
  emu->feed = (ifx_emu_feed){0};
  emu->feed_index = 0;
  emu->frame_limit = 0;
  return complete;
}

unsigned ifx_emu_overruns(const ifx_emu *emu) { return ifx_dsp_board_receive_overruns(emu->dsp); }

unsigned ifx_emu_underruns(const ifx_emu *emu) {
  return ifx_dsp_board_transmit_underruns(emu->dsp);
}

const ifx_emu_message *ifx_emu_messages(const ifx_emu *emu, unsigned *count) {
  if (count != NULL) {
    *count = emu->message_count;
  }
  return emu->messages;
}

void ifx_emu_messages_clear(ifx_emu *emu) { emu->message_count = 0; }

void ifx_emu_reset(ifx_emu *emu) {
  ifx_dsp_board_reset(emu->dsp);
  emu->start_pending = false;
  emu->clocks = 0;
  emu->dsp_cycle_acc = 0;
  emu->dsp_frame_acc = 0;
  emu->ring_valid = false;
  emu->message_count = 0;
  boot(emu);
}

void ifx_emu_battery_erase(ifx_emu *emu) { ifx_mcu_board_battery_erase(emu->mcu); }

void ifx_emu_battery_factory(ifx_emu *emu) { ifx_mcu_board_battery_factory(emu->mcu); }

const uint8_t *ifx_emu_battery(const ifx_emu *emu) { return ifx_mcu_board_battery(emu->mcu); }

bool ifx_emu_battery_save(const ifx_emu *emu, const char *path) {
  return ifx_mcu_board_battery_save(emu->mcu, path);
}

bool ifx_emu_battery_load(ifx_emu *emu, const char *path) {
  return ifx_mcu_board_battery_load(emu->mcu, path);
}

/* The ENTER CODE page keeps its number in the character buffer instead of the
 * parameter area, so the knob is the only way to set it. */
static unsigned display_number(const char *text) {
  unsigned value = 0;
  for (const char *at = text; *at != '\0'; ++at) {
    if (*at >= '0' && *at <= '9') {
      value = value * 10u + (unsigned)(*at - '0');
    } else if (value != 0u) {
      break;
    }
  }
  return value;
}

static unsigned shown_code(const ifx_emu *emu) {
  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  ifx_emu_display(emu, text);
  return display_number(text);
}

bool ifx_emu_factory_restore(ifx_emu *emu) {
  ifx_emu_reset(emu);
  if (!ifx_emu_recall(emu, IFX_EMU_PRESET_COUNT)) {
    return false;
  }
  /* Selecting by index alone lands on page 0x18, which also carries 0x27; the
   * restore page is the last one on preset 160's list. */
  ifx_emu_turn(emu, IFX_MCU_BOARD_FUNCTION_SELECT, -(int)IFX_EMU_PAGE_TURNS);
  if (!seek_page(emu, IFX_EMU_FACTORY_PAGE)) {
    return false;
  }
  ifx_emu_turn(emu, IFX_MCU_BOARD_PARAMETER_SELECT, -(int)IFX_EMU_SPECIAL_PARAM_STRIDE);
  ifx_emu_turn(emu, IFX_MCU_BOARD_PARAMETER_SELECT,
               (int)page_position(emu, IFX_EMU_FACTORY_PAGE, IFX_EMU_ENTER_CODE_INDEX) - 1);
  if (ifx_emu_index(emu) != IFX_EMU_ENTER_CODE_INDEX) {
    return false;
  }
  ifx_emu_turn(emu, IFX_MCU_BOARD_PARAMETER_ADJUST, -2 * (int)IFX_EMU_ENTER_CODE_VALUE);
  for (unsigned step = 0; step < 4u * IFX_EMU_ENTER_CODE_VALUE; ++step) {
    /* The panel paints the new code a frame or two after the detent, so the
     * display is only trusted once it has settled. */
    ifx_emu_run_ms(emu, 20);
    unsigned shown = shown_code(emu);
    if (shown == IFX_EMU_ENTER_CODE_VALUE) {
      break;
    }
    ifx_emu_turn(emu, IFX_MCU_BOARD_PARAMETER_ADJUST, shown < IFX_EMU_ENTER_CODE_VALUE ? 1 : -1);
  }
  if (shown_code(emu) != IFX_EMU_ENTER_CODE_VALUE) {
    return false;
  }
  ifx_emu_battery_erase(emu);
  ifx_emu_button(emu, IFX_MCU_BOARD_BUTTON_STORE, true);
  ifx_emu_run_ms(emu, 30);
  ifx_emu_button(emu, IFX_MCU_BOARD_BUTTON_STORE, false);
  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  for (unsigned waited = 0; waited < IFX_EMU_RESTORE_MS; waited += 1000u) {
    ifx_emu_run_ms(emu, 1000);
    ifx_emu_display(emu, text);
    if (strncmp(text, "ERRORS", 6u) == 0) {
      return display_number(text) == 0u;
    }
  }
  return false;
}
