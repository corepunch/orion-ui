#ifndef __UI_ZIP_H__
#define __UI_ZIP_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Read-only ZIP archive (stored and deflated entries) over a caller-owned memory image.
// Entry names are matched by file name only, ignoring directories and case, which is how
// skin, theme and resource packs (.wsz, .zip) are consumed.
typedef struct zip_s zip_t;

zip_t *zip_open_memory(const void *data, size_t size);      // data must outlive the zip_t
void   zip_close(zip_t *zip);
int    zip_entry_count(const zip_t *zip);
const char *zip_entry_name(const zip_t *zip, int index);    // full stored path
// Index of the first entry whose file name equals `name` (case-insensitive), or -1.
int    zip_find(const zip_t *zip, const char *name);
// Inflated bytes of an entry, NUL-terminated for convenience; release with free().
uint8_t *zip_extract(const zip_t *zip, int index, size_t *out_size);

#endif
