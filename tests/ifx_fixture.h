/* Fixture container: the firmware's answers plus what produced them, so a file
 * describes itself. Little-endian throughout.
 *
 *   16-byte header: "IFXFIX01", u32 rate, u32 section count
 *   per section:    u32 kind, u32 record size, u32 record count, records
 */
#ifndef IFX_FIXTURE_H
#define IFX_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#define IFX_FIXTURE_RATE 39000u
#define IFX_FIX_TABLE_NAME 32u
#define IFX_FIX_TABLE_INDEX_SIZE 40u
#define IFX_FIX_TABLE_WORDS_SIZE 4u

enum ifx_fixture_kind { IFX_FIX_TABLE_INDEX = 1, IFX_FIX_TABLE_WORDS = 2, IFX_FIX_KINDS = 2 };

/* One lookup table: its name and where its first word sits in the word section. */
typedef struct {
  char name[IFX_FIX_TABLE_NAME + 1u];
  uint32_t first;
  uint32_t count;
} ifx_fixture_table;

typedef struct {
  unsigned char *data;
  size_t bytes;
  size_t offset[IFX_FIX_KINDS + 1u];
  uint32_t size[IFX_FIX_KINDS + 1u];
  uint32_t count[IFX_FIX_KINDS + 1u];
} ifx_fixture;

/* Reads and validates a whole file: magic, rate, every section's kind, size and
 * count, and that the records end exactly at the end of the file. 0 on success,
 * -1 with a message on stderr. */
int ifx_fixture_load(ifx_fixture *fixture, const char *path);
void ifx_fixture_free(ifx_fixture *fixture);

const void *ifx_fixture_records(const ifx_fixture *fixture, unsigned kind, uint32_t *count,
                                uint32_t *size);

/* Accumulates records in memory and writes them to <path>.tmp at the end, so a
 * failed run leaves the previous file alone. */
typedef struct {
  char *path;
  unsigned char *data[IFX_FIX_KINDS + 1u];
  size_t used[IFX_FIX_KINDS + 1u];
  size_t cap[IFX_FIX_KINDS + 1u];
  uint32_t size[IFX_FIX_KINDS + 1u];
  uint32_t count[IFX_FIX_KINDS + 1u];
} ifx_fixture_writer;

int ifx_fixture_writer_open(ifx_fixture_writer *writer, const char *path);
int ifx_fixture_writer_add(ifx_fixture_writer *writer, unsigned kind, uint32_t size,
                           const void *record);
int ifx_fixture_writer_close(ifx_fixture_writer *writer);
void ifx_fixture_writer_abort(ifx_fixture_writer *writer);

/* Record codecs. */
void ifx_fixture_put_table(unsigned char *record, const ifx_fixture_table *table);
void ifx_fixture_get_table(const void *record, ifx_fixture_table *table);
void ifx_fixture_put_word(unsigned char *record, int32_t value);
int32_t ifx_fixture_get_word(const void *record);

#endif /* IFX_FIXTURE_H */
