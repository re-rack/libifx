#include "ifx_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 16u
#define SECTION_BYTES 12u

static const char k_magic[8] = {'I', 'F', 'X', 'F', 'I', 'X', '0', '1'};

static uint32_t get_u32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32(unsigned char *p, uint32_t value) {
  p[0] = (unsigned char)(value & 0xFFu);
  p[1] = (unsigned char)((value >> 8) & 0xFFu);
  p[2] = (unsigned char)((value >> 16) & 0xFFu);
  p[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static unsigned fixed_size(unsigned kind) {
  switch (kind) {
  case IFX_FIX_TABLE_INDEX:
    return IFX_FIX_TABLE_INDEX_SIZE;
  case IFX_FIX_TABLE_WORDS:
    return IFX_FIX_TABLE_WORDS_SIZE;
  default:
    return 0;
  }
}

int ifx_fixture_load(ifx_fixture *fixture, const char *path) {
  memset(fixture, 0, sizeof *fixture);
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "ifx: cannot open %s\n", path);
    return -1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return -1;
  }
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }
  size_t bytes = (size_t)length;
  unsigned char *data = malloc(bytes + 1u);
  if (data == NULL || fread(data, 1, bytes, file) != bytes) {
    fprintf(stderr, "ifx: cannot read %s\n", path);
    free(data);
    fclose(file);
    return -1;
  }
  fclose(file);
  fixture->data = data;
  fixture->bytes = bytes;

  if (bytes < HEADER_BYTES || memcmp(data, k_magic, sizeof k_magic) != 0) {
    fprintf(stderr, "ifx: %s is not a fixture file\n", path);
    goto fail;
  }
  if (get_u32(data + 8) != IFX_FIXTURE_RATE) {
    fprintf(stderr, "ifx: %s holds a rate of %u, not %u\n", path, get_u32(data + 8),
            IFX_FIXTURE_RATE);
    goto fail;
  }
  uint32_t sections = get_u32(data + 12);
  size_t offset = HEADER_BYTES;
  unsigned previous = 0;
  for (uint32_t s = 0; s < sections; s++) {
    if (offset + SECTION_BYTES > bytes) {
      fprintf(stderr, "ifx: %s is truncated in section %u\n", path, s);
      goto fail;
    }
    unsigned kind = get_u32(data + offset);
    uint32_t size = get_u32(data + offset + 4);
    uint32_t count = get_u32(data + offset + 8);
    offset += SECTION_BYTES;
    if (kind <= previous || kind > IFX_FIX_KINDS) {
      fprintf(stderr, "ifx: %s section %u has kind %u\n", path, s, kind);
      goto fail;
    }
    previous = kind;
    unsigned fixed = fixed_size(kind);
    if (fixed == 0u || size != fixed) {
      fprintf(stderr, "ifx: %s kind %u has record size %u, not %u\n", path, kind, size, fixed);
      goto fail;
    }
    size_t records = (size_t)size * (size_t)count;
    if (size != 0u && count != 0u && records / (size_t)count != (size_t)size) {
      fprintf(stderr, "ifx: %s section %u overflows\n", path, s);
      goto fail;
    }
    if (records > bytes - offset) {
      fprintf(stderr, "ifx: %s is truncated in section %u\n", path, s);
      goto fail;
    }
    fixture->offset[kind] = offset;
    fixture->size[kind] = size;
    fixture->count[kind] = count;
    offset += records;
  }
  if (offset != bytes) {
    fprintf(stderr, "ifx: %s has %zu trailing bytes\n", path, bytes - offset);
    goto fail;
  }
  if (fixture->count[IFX_FIX_TABLE_INDEX] != 0u) {
    if (fixture->count[IFX_FIX_TABLE_WORDS] == 0u) {
      fprintf(stderr, "ifx: %s indexes tables without their words\n", path);
      goto fail;
    }
    for (uint32_t record = 0; record < fixture->count[IFX_FIX_TABLE_INDEX]; record++) {
      ifx_fixture_table table;
      ifx_fixture_get_table(fixture->data + fixture->offset[IFX_FIX_TABLE_INDEX] +
                                (size_t)fixture->size[IFX_FIX_TABLE_INDEX] * record,
                            &table);
      if (table.count == 0u || table.first > fixture->count[IFX_FIX_TABLE_WORDS] ||
          table.count > fixture->count[IFX_FIX_TABLE_WORDS] - table.first) {
        fprintf(stderr, "ifx: %s table %u (%.32s) is outside the word section\n", path, record,
                table.name);
        goto fail;
      }
    }
  }
  return 0;

fail:
  ifx_fixture_free(fixture);
  return -1;
}

void ifx_fixture_free(ifx_fixture *fixture) {
  free(fixture->data);
  fixture->data = NULL;
  fixture->bytes = 0;
}

const void *ifx_fixture_records(const ifx_fixture *fixture, unsigned kind, uint32_t *count,
                                uint32_t *size) {
  if (kind == 0u || kind > IFX_FIX_KINDS || fixture->size[kind] == 0u) {
    return NULL;
  }
  if (count != NULL) {
    *count = fixture->count[kind];
  }
  if (size != NULL) {
    *size = fixture->size[kind];
  }
  return fixture->data + fixture->offset[kind];
}

int ifx_fixture_writer_open(ifx_fixture_writer *writer, const char *path) {
  memset(writer, 0, sizeof *writer);
  size_t length = strlen(path);
  writer->path = malloc(length + 1u);
  if (writer->path == NULL) {
    return -1;
  }
  memcpy(writer->path, path, length + 1u);
  return 0;
}

int ifx_fixture_writer_add(ifx_fixture_writer *writer, unsigned kind, uint32_t size,
                           const void *record) {
  if (kind == 0u || kind > IFX_FIX_KINDS || size == 0u) {
    return -1;
  }
  size_t *used = &writer->used[kind];
  if (writer->size[kind] != 0u && writer->size[kind] != size) {
    return -1;
  }
  writer->size[kind] = size;
  if (*used + size > writer->cap[kind]) {
    size_t cap = writer->cap[kind] == 0u ? 4096u : writer->cap[kind];
    while (cap < *used + size) {
      cap *= 2u;
    }
    unsigned char *grown = realloc(writer->data[kind], cap);
    if (grown == NULL) {
      return -1;
    }
    writer->data[kind] = grown;
    writer->cap[kind] = cap;
  }
  memcpy(writer->data[kind] + *used, record, size);
  *used += size;
  writer->count[kind]++;
  return 0;
}

int ifx_fixture_writer_close(ifx_fixture_writer *writer) {
  if (writer->path == NULL) {
    return -1;
  }
  size_t length = strlen(writer->path);
  char *tmp = malloc(length + sizeof ".tmp");
  int status = -1;
  if (tmp == NULL) {
    goto done;
  }
  memcpy(tmp, writer->path, length);
  memcpy(tmp + length, ".tmp", sizeof ".tmp");
  FILE *file = fopen(tmp, "wb");
  if (file == NULL) {
    free(tmp);
    goto done;
  }
  unsigned char header[HEADER_BYTES];
  uint32_t sections = 0;
  for (unsigned kind = 1; kind <= IFX_FIX_KINDS; kind++) {
    if (writer->count[kind] != 0u) {
      sections++;
    }
  }
  memcpy(header, k_magic, sizeof k_magic);
  put_u32(header + 8, IFX_FIXTURE_RATE);
  put_u32(header + 12, sections);
  int ok = fwrite(header, 1, sizeof header, file) == sizeof header;
  for (unsigned kind = 1; ok && kind <= IFX_FIX_KINDS; kind++) {
    if (writer->count[kind] == 0u) {
      continue;
    }
    unsigned char section[SECTION_BYTES];
    put_u32(section, kind);
    put_u32(section + 4, writer->size[kind]);
    put_u32(section + 8, writer->count[kind]);
    ok = fwrite(section, 1, sizeof section, file) == sizeof section &&
         fwrite(writer->data[kind], 1, writer->used[kind], file) == writer->used[kind];
  }
  ok = fclose(file) == 0 && ok;
  if (ok && rename(tmp, writer->path) != 0) {
    ok = 0;
  }
  if (!ok) {
    remove(tmp);
  }
  free(tmp);
  status = ok ? 0 : -1;
done:
  ifx_fixture_writer_abort(writer);
  return status;
}

void ifx_fixture_writer_abort(ifx_fixture_writer *writer) {
  for (unsigned kind = 1; kind <= IFX_FIX_KINDS; kind++) {
    free(writer->data[kind]);
    writer->data[kind] = NULL;
  }
  free(writer->path);
  writer->path = NULL;
  memset(writer->used, 0, sizeof writer->used);
  memset(writer->cap, 0, sizeof writer->cap);
  memset(writer->size, 0, sizeof writer->size);
  memset(writer->count, 0, sizeof writer->count);
}

void ifx_fixture_put_table(unsigned char *record, const ifx_fixture_table *table) {
  memset(record, 0, IFX_FIX_TABLE_INDEX_SIZE);
  size_t length = strlen(table->name);
  if (length >= IFX_FIX_TABLE_NAME) {
    length = IFX_FIX_TABLE_NAME - 1u;
  }
  memcpy(record, table->name, length);
  put_u32(record + IFX_FIX_TABLE_NAME, table->first);
  put_u32(record + IFX_FIX_TABLE_NAME + 4u, table->count);
}

void ifx_fixture_get_table(const void *record, ifx_fixture_table *table) {
  const unsigned char *bytes = record;
  memcpy(table->name, bytes, IFX_FIX_TABLE_NAME);
  table->name[IFX_FIX_TABLE_NAME] = '\0';
  table->first = get_u32(bytes + IFX_FIX_TABLE_NAME);
  table->count = get_u32(bytes + IFX_FIX_TABLE_NAME + 4u);
}

static int32_t word24(const unsigned char *p) {
  uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  return (value & 0x800000u) != 0u ? (int32_t)(value - 0x1000000u) : (int32_t)value;
}

static void put_word24(unsigned char *p, int32_t value) {
  uint32_t word = (uint32_t)value & 0xFFFFFFu;
  p[0] = (unsigned char)(word & 0xFFu);
  p[1] = (unsigned char)((word >> 8) & 0xFFu);
  p[2] = (unsigned char)((word >> 16) & 0xFFu);
}

void ifx_fixture_put_word(unsigned char *record, int32_t value) {
  put_word24(record, value);
  record[3] = 0;
}

int32_t ifx_fixture_get_word(const void *record) { return word24(record); }
