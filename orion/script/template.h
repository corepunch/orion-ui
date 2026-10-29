#ifndef __ORION_SCRIPT_TEMPLATE_H__
#define __ORION_SCRIPT_TEMPLATE_H__
#include <stdbool.h>
#include <stddef.h>

// A native data source for the Lua template plugin. Row 0 is the root;
// other row handles are owned by the caller, valid until rendering returns.
typedef void (*script_template_emit_t)(int row, void *target);
typedef struct {
  void *userdata;
  bool (*resolve)(void *userdata, int row, const char *name, char *out, size_t size);
  bool (*rows)(void *userdata, const char *name, int parent, script_template_emit_t emit, void *target);
  bool (*filter)(void *userdata, const char *name, const char *value, char *out, size_t size);
} script_template_source_t;

// Returns a malloc-owned string in *output. Lua owns parsing, blocks, filters
// and escaping; the source provides data and optional domain formatters.
bool script_template_render(const char *text, const char *format, const script_template_source_t *source,
                            char **output, char *error, size_t error_size);
#endif
