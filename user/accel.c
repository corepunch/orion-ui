// Accelerator table implementation.
// Maps keyboard shortcuts (key + modifiers) to command IDs and delivers them
// as kWindowMessageCommand events, mirroring WinAPI TranslateAccelerator.

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

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
  if (evt->type != SDL_KEYDOWN) return false;

  SDL_Keymod   mod = SDL_GetModState();
  uint16_t     sc  = (uint16_t)evt->key.keysym.scancode;

  for (int i = 0; i < table->count; i++) {
    const accel_t *a = &table->entries[i];
    if (a->key != sc) continue;
    bool want_ctrl  = (a->fVirt & FCONTROL) != 0;
    bool want_shift = (a->fVirt & FSHIFT)   != 0;
    bool want_alt   = (a->fVirt & FALT)     != 0;
    bool has_ctrl   = (mod & KMOD_CTRL)  != 0;
    bool has_shift  = (mod & KMOD_SHIFT) != 0;
    bool has_alt    = (mod & KMOD_ALT)   != 0;
    if (has_ctrl == want_ctrl && has_shift == want_shift && has_alt == want_alt) {
      // Suppress accelerators that require no Ctrl or Alt while a text-editing
      // control has keyboard focus.  This mirrors the WinAPI behaviour where
      // an edit control captures character input before TranslateAccelerator
      // can intercept it, so bare-key shortcuts (e.g. P/B/E/K/S for tools)
      // don't fire while the user is typing.  Accelerators that require
      // Ctrl or Alt are never suppressed.
      if (!want_ctrl && !want_alt && _focused && _focused->editing) continue;
      send_message(win, kWindowMessageCommand,
                   MAKEDWORD(a->cmd, kAcceleratorNotification), NULL);
      return true;
    }
  }
  return false;
}
