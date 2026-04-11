// Accelerator table implementation.
// Maps keyboard shortcuts (key + modifiers) to command IDs and delivers them
// as kWindowMessageCommand events, mirroring WinAPI TranslateAccelerator.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "user.h"
#include "messages.h"
#include "accel.h"

struct accel_table_s {
  accel_t *entries;
  int      count;
};

accel_table_t *load_accelerators(const accel_t *entries, int count) {
  if (!entries || count <= 0) return NULL;
  accel_table_t *tbl = malloc(sizeof(accel_table_t));
  if (!tbl) return NULL;
  tbl->entries = malloc(sizeof(accel_t) * (size_t)count);
  if (!tbl->entries) { free(tbl); return NULL; }
  memcpy(tbl->entries, entries, sizeof(accel_t) * (size_t)count);
  tbl->count = count;
  return tbl;
}

void free_accelerators(accel_table_t *table) {
  if (!table) return;
  free(table->entries);
  free(table);
}

bool translate_accelerator(window_t *win, ui_event_t *evt,
                           accel_table_t *table) {
  if (!win || !evt || !table) return false;
  if (evt->message != kEventKeyDown) return false;

  uint32_t sc  = evt->keyCode;
  uint32_t mod = evt->modflags;

  for (int i = 0; i < table->count; i++) {
    const accel_t *a = &table->entries[i];
    if (a->key != sc) continue;
    bool want_ctrl  = (a->fVirt & FCONTROL) != 0;
    bool want_shift = (a->fVirt & FSHIFT)   != 0;
    bool want_alt   = (a->fVirt & FALT)     != 0;
#ifdef __APPLE__
    bool has_ctrl   = (mod & (WI_MOD_CMD  >> 16)) != 0;
#else
    bool has_ctrl   = (mod & (WI_MOD_CTRL >> 16)) != 0;
#endif
    bool has_shift  = (mod & (WI_MOD_SHIFT >> 16)) != 0;
    bool has_alt    = (mod & (WI_MOD_ALT  >> 16)) != 0;
    if (has_ctrl == want_ctrl && has_shift == want_shift && has_alt == want_alt) {
      // Suppress accelerators that require no Ctrl or Alt while a text-editing
      // control has keyboard focus, mirroring WinAPI TranslateAccelerator.
      if (!want_ctrl && !want_alt && _focused && _focused->editing) continue;
      send_message(win, kWindowMessageCommand,
                   MAKEDWORD(a->cmd, kAcceleratorNotification), NULL);
      return true;
    }
  }
  return false;
}

const accel_t *accel_find_cmd(const accel_table_t *table, uint16_t cmd) {
  if (!table) return NULL;
  for (int i = 0; i < table->count; i++) {
    if (table->entries[i].cmd == cmd)
      return &table->entries[i];
  }
  return NULL;
}

int accel_format(const accel_t *a, char *buf, int bufsize) {
  if (!a || !buf || bufsize <= 0) return 0;
  const char *kname = WI_KeynumToString((uint32_t)a->key);
  return snprintf(buf, (size_t)bufsize, "%s%s%s%s",
                  (a->fVirt & FCONTROL) ? "Ctrl+"  : "",
                  (a->fVirt & FSHIFT)   ? "Shift+" : "",
                  (a->fVirt & FALT)     ? "Alt+"   : "",
                  kname ? kname : "");
}
