// Accelerator table implementation.
// Maps keyboard shortcuts (key + modifiers) to command IDs and delivers them
// as evCommand events, mirroring WinAPI TranslateAccelerator.

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

bool accel_parse(const char *spec, accel_t *a) {
  static const struct { const char *name; uint16_t key; } names[] = {
    {"SPACE", AX_KEY_SPACE},   {"ENTER", AX_KEY_ENTER},   {"RETURN", AX_KEY_ENTER}, {"ESCAPE", AX_KEY_ESCAPE},
    {"ESC", AX_KEY_ESCAPE},    {"BACKSPACE", AX_KEY_BACKSPACE}, {"TAB", AX_KEY_TAB},  {"HOME", AX_KEY_HOME},
    {"END", AX_KEY_END},       {"LEFT", AX_KEY_LEFTARROW}, {"RIGHT", AX_KEY_RIGHTARROW}, {"UP", AX_KEY_UPARROW},
    {"DOWN", AX_KEY_DOWNARROW}, {"PAGEUP", AX_KEY_PGUP},  {"PAGEDOWN", AX_KEY_PGDN},  {"DELETE", AX_KEY_DEL},
    {"DEL", AX_KEY_DEL},       {"INSERT", AX_KEY_INS},    {"EQUALS", AX_KEY_EQUALS}, {"MINUS", AX_KEY_MINUS},
    {"SLASH", AX_KEY_SLASH},
  };
  char buf[64], *tok;
  if (!a || !spec || !*spec || strlen(spec) >= sizeof(buf)) goto rejected;
  snprintf(buf, sizeof(buf), "%s", spec);
  a->fVirt = FVIRTKEY;
  a->key = 0;
  tok = buf;
  for (char *p = buf; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
  for (char *sep; tok; tok = sep) {
    if ((sep = strchr(tok, '+'))) *sep++ = 0;
    char *w = tok, *r = tok;   // "Page Up" is one name
    for (; *r; r++) if (*r != ' ') *w++ = *r;
    *w = 0;
    if (!strcmp(tok, "CTRL") || !strcmp(tok, "CMD")) a->fVirt |= FCONTROL;
    else if (!strcmp(tok, "SHIFT")) a->fVirt |= FSHIFT;
    else if (!strcmp(tok, "ALT")) a->fVirt |= FALT;
    else if (a->key || sep) goto rejected;   // one key, last
    else if (tok[0] == 'F' && tok[1] >= '1' && tok[1] <= '9' && atoi(tok + 1) >= 1 && atoi(tok + 1) <= 12 &&
             strspn(tok + 1, "0123456789") == strlen(tok + 1)) a->key = (uint16_t)(AX_KEY_F1 + atoi(tok + 1) - 1);
    else if (((tok[0] >= 'A' && tok[0] <= 'Z') || (tok[0] >= '0' && tok[0] <= '9')) && !tok[1]) a->key = (uint16_t)tok[0];
    else for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && !a->key; i++) if (!strcmp(tok, names[i].name)) a->key = names[i].key;
    if (!a->key && !sep && strcmp(tok, "CTRL") && strcmp(tok, "CMD") && strcmp(tok, "SHIFT") && strcmp(tok, "ALT")) goto rejected;
  }
  if (a->key) return true;
rejected:
  fprintf(stderr, "[accel] accel_parse rejected shortcut '%s'\n", spec ? spec : "(null)");
  fflush(stderr);
  return false;
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
  /* Normalize lowercase letter keys to uppercase: the platform may send
     97-122 ('a'-'z') for unshifted letter keys, while AX_KEY_A..AX_KEY_Z
     are defined as uppercase ASCII 65-90. */
  if (sc >= 'a' && sc <= 'z') sc -= (uint32_t)('a' - 'A');
  uint32_t mod = evt->modflags;

  for (int i = 0; i < table->count; i++) {
    const accel_t *a = &table->entries[i];
    if (a->key != sc) continue;
    bool want_ctrl  = (a->fVirt & FCONTROL) != 0;
    bool want_shift = (a->fVirt & FSHIFT)   != 0;
    bool want_alt   = (a->fVirt & FALT)     != 0;
#ifdef __APPLE__
    bool has_ctrl   = (mod & (AX_MOD_CMD  >> 16)) != 0;
#else
    bool has_ctrl   = (mod & (AX_MOD_CTRL >> 16)) != 0;
#endif
    bool has_shift  = (mod & (AX_MOD_SHIFT >> 16)) != 0;
    bool has_alt    = (mod & (AX_MOD_ALT  >> 16)) != 0;
    if (has_ctrl == want_ctrl && has_shift == want_shift && has_alt == want_alt) {
      // Suppress accelerators that require no Ctrl or Alt while a text-editing
      // control has keyboard focus, mirroring WinAPI TranslateAccelerator.
        if (!want_ctrl && !want_alt && g_ui_runtime.focused &&
          window_has_state(g_ui_runtime.focused, WINDOW_STATE_EDITING)) continue;
      send_message(win, evCommand,
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
  const char *kname = axKeynumToString((uint32_t)a->key);
  int prefix = snprintf(buf, (size_t)bufsize, "%s%s%s",
                        (a->fVirt & FCONTROL) ? "Ctrl+"  : "",
                        (a->fVirt & FSHIFT)   ? "Shift+" : "",
                        (a->fVirt & FALT)     ? "Alt+"   : "");
  int n = snprintf(buf + MIN(prefix, bufsize - 1), (size_t)(bufsize - MIN(prefix, bufsize - 1)), "%s", kname ? kname : "");
  // Platform key names are lower case ("f9", "escape"); labels read "F9", "Escape".
  if (prefix < bufsize - 1 && buf[prefix] >= 'a' && buf[prefix] <= 'z') buf[prefix] = (char)(buf[prefix] - 'a' + 'A');
  return prefix + n;
}

uint32_t accel_modifiers(const accel_t *a) {
  if (!a) return 0;
#ifdef __APPLE__
  uint32_t ctrl = AX_MOD_CMD;
#else
  uint32_t ctrl = AX_MOD_CTRL;
#endif
  return ((a->fVirt & FCONTROL) ? ctrl : 0) | ((a->fVirt & FSHIFT) ? AX_MOD_SHIFT : 0) | ((a->fVirt & FALT) ? AX_MOD_ALT : 0);
}
