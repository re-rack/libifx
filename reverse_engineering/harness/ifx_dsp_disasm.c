/* DSP56001 disassembler over a raw 24-bit word image, the same command line as
 * dsp56300-disasm so build_workspace.py can use either.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hatari/dsp_core.h"
#include "hatari/dsp_cpu.h"
#include "hatari/dsp_disasm.h"
#include "ifx_dsp_board.h"

#define IFX_DISASM_WORD_BYTES 3

static void usage(FILE *fp) {
  fprintf(fp, "usage: ifx_dsp_disasm [-f FORMAT] [-s OFFSET] [-n COUNT] [--color WHEN] FILE\n"
              "  -f u24be    input words are 24-bit big endian (default)\n"
              "  -s OFFSET   P address of the first word, hex (default 0)\n"
              "  -n COUNT    number of instructions to disassemble (default: all)\n"
              "  --color     accepted and ignored\n");
}

static int parse_hex(const char *text, unsigned long *out) {
  char *end;
  errno = 0;
  *out = strtoul(text, &end, 16);
  return errno == 0 && *end == '\0' && end != text ? 0 : -1;
}

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "ifx_dsp_disasm: %s: %s\n", path, strerror(errno));
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long length = ftell(fp);
  if (length < 0 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  uint8_t *data = malloc((size_t)length + 1);
  if (data == NULL) {
    fclose(fp);
    return NULL;
  }
  if (fread(data, 1, (size_t)length, fp) != (size_t)length) {
    free(data);
    fclose(fp);
    return NULL;
  }
  fclose(fp);
  *size = (size_t)length;
  return data;
}

int main(int argc, char *argv[]) {
  const char *format = "u24be";
  const char *path = NULL;
  unsigned long start = 0;
  unsigned long wanted = 0;
  int have_count = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(stdout);
      return 0;
    }
    if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
      format = argv[++i];
    } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      if (parse_hex(argv[++i], &start) != 0) {
        usage(stderr);
        return 2;
      }
    } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      if (parse_hex(argv[++i], &wanted) != 0) {
        usage(stderr);
        return 2;
      }
      have_count = 1;
    } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
      i++;
    } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
      fprintf(stderr, "ifx_dsp_disasm: unknown option %s\n", argv[i]);
      usage(stderr);
      return 2;
    } else if (path == NULL) {
      path = argv[i];
    } else {
      usage(stderr);
      return 2;
    }
  }
  if (strcmp(format, "u24be") != 0) {
    fprintf(stderr, "ifx_dsp_disasm: only u24be input is supported\n");
    return 2;
  }
  if (path == NULL) {
    usage(stderr);
    return 2;
  }

  size_t size = 0;
  uint8_t *data = read_file(path, &size);
  if (data == NULL) {
    return 1;
  }
  if (size % IFX_DISASM_WORD_BYTES != 0) {
    fprintf(stderr, "ifx_dsp_disasm: %s: size %zu is not a whole number of 24-bit words\n", path,
            size);
    free(data);
    return 1;
  }
  size_t words = size / IFX_DISASM_WORD_BYTES;

  ifx_dsp_board *board = ifx_dsp_board_create();
  if (board == NULL) {
    free(data);
    return 1;
  }
  for (size_t i = 0; i < words; i++) {
    uint32_t word = ((uint32_t)data[i * IFX_DISASM_WORD_BYTES] << 16) |
                    ((uint32_t)data[i * IFX_DISASM_WORD_BYTES + 1] << 8) |
                    data[i * IFX_DISASM_WORD_BYTES + 2];
    ifx_dsp_board_store(board, DSP_SPACE_P, (uint32_t)(start + i), word);
  }
  free(data);
  dsp56k_disasm_init();

  unsigned long done = 0;
  for (uint32_t index = 0; index < words; index++) {
    if (have_count && done >= wanted) {
      break;
    }
    uint32_t pc = (uint32_t)(start + index);
    dsp_core.pc = (uint16_t)pc;
    uint16_t length = dsp56k_disasm(DSP_DISASM_MODE, stdout);
    uint32_t first = ifx_dsp_board_word(board, DSP_SPACE_P, pc);
    char second[8] = "       ";
    if (length > 1) {
      uint32_t operand = 0;
      if (index + 1 < words) {
        operand = ifx_dsp_board_word(board, DSP_SPACE_P, pc + 1);
      }
      snprintf(second, sizeof(second), " %06X", (unsigned)operand);
    }
    printf("P:$%06X  %06X%s  %s\n", (unsigned)pc, (unsigned)first, second,
           dsp56k_getInstructionOnlyText());
    index += length > 1 ? (uint32_t)(length - 1) : 0;
    done++;
  }

  ifx_dsp_board_destroy(board);
  return 0;
}
