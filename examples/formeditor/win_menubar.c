// Menu bar, document management, file I/O, and dialog entry points
// for the Orion Form Editor.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// ============================================================
// Menu definitions
// ============================================================

static const menu_item_t kFileItems[] = {
  {"New",        ID_FILE_NEW},
  {"Open...",    ID_FILE_OPEN},
  {NULL,         0},
  {"Save",       ID_FILE_SAVE},
  {"Save As...", ID_FILE_SAVEAS},
  {NULL,         0},
  {"Quit",       ID_FILE_QUIT},
};

static const menu_item_t kEditItems[] = {
  {"Delete\tDel",       ID_EDIT_DELETE},
  {NULL,                0},
  {"Properties...",     ID_EDIT_PROPS},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Edit", kEditItems, (int)(sizeof(kEditItems)/sizeof(kEditItems[0]))},
  {"Help", kHelpItems, (int)(sizeof(kHelpItems)/sizeof(kHelpItems[0]))},
};
const int kNumMenus = (int)(sizeof(kMenus)/sizeof(kMenus[0]));

// ============================================================
// Document title
// ============================================================

void form_doc_update_title(form_doc_t *doc) {
  if (!doc || !doc->doc_win) return;
  const char *name = doc->filename[0] ? doc->filename : "Untitled";
  const char *slash = strrchr(name, '/');
  if (slash) name = slash + 1;
  char title[64];
  snprintf(title, sizeof(title), "%s%s", name, doc->modified ? " *" : "");
  strncpy(doc->doc_win->title, title, sizeof(doc->doc_win->title) - 1);
  doc->doc_win->title[sizeof(doc->doc_win->title) - 1] = '\0';
  invalidate_window(doc->doc_win);
}

// ============================================================
// Document window procedure
// ============================================================

static result_t doc_win_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  form_doc_t *doc = (form_doc_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      return true;
    case kWindowMessagePaint:
      fill_rect(get_sys_color(kColorWorkspaceBg), 0, 0, win->frame.w, win->frame.h);
      return false;
    case kWindowMessageHScroll:
      // Forward the built-in hscroll notification to the canvas child.
      if (doc && doc->canvas_win)
        send_message(doc->canvas_win, kWindowMessageHScroll, wparam, lparam);
      return true;
    case kWindowMessageResize:
      if (doc && doc->canvas_win)
        resize_window(doc->canvas_win, win->frame.w, win->frame.h);
      return false;
    case kWindowMessageClose: {
      if (!doc) return false;
      if (doc->modified) {
        int res = message_box(win,
                              "This form has unsaved changes.\nClose without saving?",
                              "Unsaved Changes", MB_YESNOCANCEL);
        if (res == IDCANCEL) return true;  // cancel close
        // IDNO: discard. IDYES with no filename: also discard (no auto-save for .h).
        if (res == IDYES && doc->filename[0])
          form_save(doc, doc->filename);
      }
      close_form_doc(doc);
      return true;
    }
    default:
      return false;
  }
}

// ============================================================
// create_form_doc / close_form_doc
// ============================================================

form_doc_t *create_form_doc(int w, int h) {
  if (!g_app) return NULL;
  if (w <= 0 || h <= 0) return NULL;

  // Close existing document first (single-document editor).
  if (g_app->doc)
    close_form_doc(g_app->doc);

  form_doc_t *doc = (form_doc_t *)calloc(1, sizeof(form_doc_t));
  if (!doc) return NULL;

  doc->form_w    = w;
  doc->form_h    = h;
  doc->modified  = false;
  doc->next_id   = CTRL_ID_BASE;

  // Document window
  window_t *dwin = create_window(
      "Untitled",
      WINDOW_STATUSBAR | WINDOW_HSCROLL,
      MAKERECT(DOC_START_X, DOC_START_Y, DOC_WIN_W, DOC_WIN_H),
      NULL, doc_win_proc, NULL);
  dwin->userdata = doc;
  doc->doc_win   = dwin;

  // Canvas child window (owns the VSCROLL)
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
      MAKERECT(0, 0, DOC_WIN_W, DOC_WIN_H),
      dwin, win_canvas_proc, doc);
  cwin->notabstop = false;
  doc->canvas_win = cwin;

  g_app->doc = doc;

  show_window(dwin, true);
  form_doc_update_title(doc);
  send_message(dwin, kWindowMessageStatusBar, 0, (void *)"New form");
  return doc;
}

void close_form_doc(form_doc_t *doc) {
  if (!doc) return;
  if (g_app && g_app->doc == doc)
    g_app->doc = NULL;
  if (doc->doc_win && is_window(doc->doc_win))
    destroy_window(doc->doc_win);
  free(doc);
}

// ============================================================
// File I/O — save/load as a C .h header
// ============================================================

// Map control type to a short keyword used in the file.
static const char *ctrl_type_token(int type) {
  switch (type) {
    case CTRL_BUTTON:   return "button";
    case CTRL_CHECKBOX: return "checkbox";
    case CTRL_LABEL:    return "label";
    case CTRL_TEXTEDIT: return "textedit";
    case CTRL_LIST:     return "list";
    case CTRL_COMBOBOX: return "combobox";
    default:            return "control";
  }
}

// Map control type to the FORM_CTRL_* enum name (uppercase suffix).
static const char *ctrl_type_form_token(int type) {
  switch (type) {
    case CTRL_BUTTON:   return "BUTTON";
    case CTRL_CHECKBOX: return "CHECKBOX";
    case CTRL_LABEL:    return "LABEL";
    case CTRL_TEXTEDIT: return "TEXTEDIT";
    case CTRL_LIST:     return "LIST";
    case CTRL_COMBOBOX: return "COMBOBOX";
    default:            return "BUTTON";
  }
}

static int ctrl_type_from_token(const char *tok) {
  if (strcmp(tok, "button")   == 0) return CTRL_BUTTON;
  if (strcmp(tok, "checkbox") == 0) return CTRL_CHECKBOX;
  if (strcmp(tok, "label")    == 0) return CTRL_LABEL;
  if (strcmp(tok, "textedit") == 0) return CTRL_TEXTEDIT;
  if (strcmp(tok, "list")     == 0) return CTRL_LIST;
  if (strcmp(tok, "combobox") == 0) return CTRL_COMBOBOX;
  return -1;
}

// Sanitize a string for embedding in a C comment block: strips '"', '*', '/',
// '\n', '\r' to prevent comment-termination injection.
static void sanitize_c_comment_str(const char *src, char *dst, size_t dst_sz) {
  size_t di = 0;
  for (size_t si = 0; src[si] && di < dst_sz - 1; si++) {
    char ch = src[si];
    if (ch == '"' || ch == '*' || ch == '/' || ch == '\n' || ch == '\r')
      continue;
    dst[di++] = ch;
  }
  dst[di] = '\0';
}

// Sanitize a string for embedding in a C string literal: escapes '\\'
// and strips '"', '\n', '\r'.
static void sanitize_c_str_literal(const char *src, char *dst, size_t dst_sz) {
  size_t di = 0;
  for (size_t si = 0; src[si] && di < dst_sz - 2; si++) {
    char ch = src[si];
    if (ch == '\\') {
      dst[di++] = '\\';
      dst[di++] = '\\';
    } else if (ch == '"' || ch == '\n' || ch == '\r') {
      continue;
    } else {
      dst[di++] = ch;
    }
  }
  dst[di] = '\0';
}

// Extract a C identifier from a file path: strips directory component and
// extension, then replaces non-identifier characters with '_'.
static void path_to_form_ident(const char *path, char *ident, size_t ident_sz) {
  const char *base = strrchr(path, '/');
  if (!base) base = strrchr(path, '\\');
  base = base ? base + 1 : path;
  size_t di = 0;
  for (size_t si = 0; base[si] && base[si] != '.' && di < ident_sz - 1; si++) {
    char ch = base[si];
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_')
      ident[di++] = ch;
    else
      ident[di++] = '_';
  }
  if (di == 0) { ident[0] = 'f'; di = 1; }
  // Ensure identifier doesn't start with a digit.
  if (ident[0] >= '0' && ident[0] <= '9') {
    if (di < ident_sz - 1) {
      memmove(ident + 1, ident, di + 1);
      ident[0] = 'f';
      di++;
    }
  }
  ident[di] = '\0';
}

// Return true when s is a non-empty, valid C identifier.
static bool is_c_identifier(const char *s) {
  if (!s || !s[0]) return false;
  if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_'))
    return false;
  for (const char *p = s + 1; *p; p++) {
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '_'))
      return false;
  }
  return true;
}

bool form_save(form_doc_t *doc, const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) return false;

  fprintf(f, "/* ORION_FORM_BEGIN */\n");
  fprintf(f, "/* form %d %d */\n", doc->form_w, doc->form_h);
  for (int i = 0; i < doc->element_count; i++) {
    form_element_t *el = &doc->elements[i];
    char safe_text[sizeof(el->text)];
    char safe_name[sizeof(el->name)];
    sanitize_c_comment_str(el->text, safe_text, sizeof(safe_text));
    sanitize_c_comment_str(el->name, safe_name, sizeof(safe_name));
    fprintf(f, "/* ctrl %s %d %d %d %d %d \"%s\" \"%s\" */\n",
            ctrl_type_token(el->type),
            el->id, el->x, el->y, el->w, el->h,
            safe_text, safe_name);
  }
  fprintf(f, "/* ORION_FORM_END */\n\n");

  // Emit #defines for control IDs so the header is usable directly.
  // Only emit when safe_name is a valid C identifier.
  for (int i = 0; i < doc->element_count; i++) {
    form_element_t *el = &doc->elements[i];
    char safe_name[sizeof(el->name)];
    sanitize_c_comment_str(el->name, safe_name, sizeof(safe_name));
    if (is_c_identifier(safe_name))
      fprintf(f, "#define %-30s %d\n", safe_name, el->id);
  }

  // Emit a form_def_t struct literal that can be passed directly to
  // create_window_from_form() at runtime, without re-parsing the file.
  char ident[64];
  path_to_form_ident(path, ident, sizeof(ident));

  fprintf(f, "\n/* Form definition — pass k%s to create_window_from_form() */\n", ident);
  if (doc->element_count > 0) {
    fprintf(f, "static const form_ctrl_def_t k%s_children[] = {\n", ident);
    for (int i = 0; i < doc->element_count; i++) {
      form_element_t *el = &doc->elements[i];
      char safe_text[sizeof(el->text) * 2];
      char safe_name[sizeof(el->name) * 2];
      sanitize_c_str_literal(el->text, safe_text, sizeof(safe_text));
      sanitize_c_str_literal(el->name, safe_name, sizeof(safe_name));
      fprintf(f, "  { FORM_CTRL_%s, %d, %d, %d, %d, %d, 0, \"%s\", \"%s\" },\n",
              ctrl_type_form_token(el->type), el->id,
              el->x, el->y, el->w, el->h,
              safe_text, safe_name);
    }
    fprintf(f, "};\n");
    fprintf(f, "static const form_def_t k%s = {\n", ident);
    fprintf(f, "  \"%s\", %d, %d, 0,\n", ident, doc->form_w, doc->form_h);
    fprintf(f, "  k%s_children,\n", ident);
    fprintf(f, "  (int)(sizeof(k%s_children) / sizeof(k%s_children[0]))\n",
            ident, ident);
    fprintf(f, "};\n");
  } else {
    fprintf(f, "static const form_def_t k%s = {\n", ident);
    fprintf(f, "  \"%s\", %d, %d, 0, NULL, 0\n", ident, doc->form_w, doc->form_h);
    fprintf(f, "};\n");
  }

  fclose(f);
  return true;
}

bool form_load(form_doc_t *doc, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return false;

  bool found_begin = false;
  bool found_end   = false;
  char line[512];

  // Reset document content
  doc->element_count = 0;
  doc->form_w        = FORM_DEFAULT_W;
  doc->form_h        = FORM_DEFAULT_H;
  memset(doc->type_counters, 0, sizeof(doc->type_counters));
  doc->next_id = CTRL_ID_BASE;

  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, "ORION_FORM_BEGIN"))  { found_begin = true; continue; }
    if (strstr(line, "ORION_FORM_END"))    { found_end   = true; break;    }
    if (!found_begin) continue;

    // /* form W H */
    {
      int fw = 0, fh = 0;
      if (sscanf(line, " /* form %d %d", &fw, &fh) == 2 && fw > 0 && fh > 0) {
        doc->form_w = fw;
        doc->form_h = fh;
        continue;
      }
    }

    // /* ctrl TYPE ID X Y W H "text" "name" */
    if (doc->element_count < MAX_ELEMENTS) {
      char type_tok[32] = {0};
      int id = 0, x = 0, y = 0, w = 0, h = 0;
      char text[64] = {0};
      char name[32] = {0};
      // sscanf with %s and %[^"] to parse the quoted strings
      int n = sscanf(line, " /* ctrl %31s %d %d %d %d %d \"%63[^\"]\" \"%31[^\"]",
                     type_tok, &id, &x, &y, &w, &h, text, name);
      if (n >= 6) {
        int type = ctrl_type_from_token(type_tok);
        if (type >= 0 && type < CTRL_TYPE_COUNT) {
          form_element_t *el = &doc->elements[doc->element_count];
          el->type  = type;
          el->id    = id;
          el->x     = x;
          el->y     = y;
          el->w     = w > 0 ? w : 10;
          el->h     = h > 0 ? h : 8;
          el->flags = 0;
          strncpy(el->text, text, sizeof(el->text) - 1);
          el->text[sizeof(el->text) - 1] = '\0';
          strncpy(el->name, name, sizeof(el->name) - 1);
          el->name[sizeof(el->name) - 1] = '\0';
          doc->element_count++;
          // Keep next_id above any loaded ID
          if (id >= doc->next_id) doc->next_id = id + 1;
          if (type >= 0 && type < CTRL_TYPE_COUNT)
            doc->type_counters[type]++;
        }
      }
    }
  }

  fclose(f);
  return found_begin && found_end;
}

// ============================================================
// About dialog
// ============================================================

#define ABOUT_W 220
#define ABOUT_H  80

static result_t about_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case kWindowMessageCreate: {
      // Info labels
      create_window("Orion Form Editor", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 8, ABOUT_W - 16, CONTROL_HEIGHT),
          win, win_label, NULL);
      create_window("Version 1.0", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 22, ABOUT_W - 16, CONTROL_HEIGHT),
          win, win_label, (void *)(uintptr_t)kColorTextDisabled);
      create_window("VB3-inspired form designer", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 36, ABOUT_W - 16, CONTROL_HEIGHT),
          win, win_label, (void *)(uintptr_t)kColorTextDisabled);
      // OK button
      create_window("OK", BUTTON_DEFAULT,
          MAKERECT(ABOUT_W - 54, ABOUT_H - 17, 50, BUTTON_HEIGHT),
          win, win_button, NULL);
      return true;
    }
    case kWindowMessageCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        end_dialog(win, 1);
        return true;
      }
      return false;
    default:
      return false;
  }
}

void show_about_dialog(window_t *parent) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  show_dialog("About Orion Form Editor",
              MAKERECT((sw - ABOUT_W) / 2, (sh - ABOUT_H) / 2, ABOUT_W, ABOUT_H),
              parent, about_proc, NULL);
}

// ============================================================
// Properties dialog
// ============================================================

#define PROPS_W  260
#define PROPS_H  110

// Child IDs
#define PROPS_ID_CAPTION   1
#define PROPS_ID_NAME      2
#define PROPS_ID_OK        3
#define PROPS_ID_CANCEL    4

typedef struct {
  form_element_t *el;
  bool            accepted;
} props_state_t;

static result_t props_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  props_state_t *ps = (props_state_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      ps = (props_state_t *)lparam;
      win->userdata = ps;

      int row = 4;
      // Caption row
      create_window("Caption:", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(4, row, 60, CONTROL_HEIGHT), win, win_label, NULL);
      window_t *cap = create_window(ps->el->text, 0,
          MAKERECT(68, row, PROPS_W - 72, BUTTON_HEIGHT),
          win, win_textedit, NULL);
      cap->id = PROPS_ID_CAPTION;

      row += BUTTON_HEIGHT + 6;
      // Name row
      create_window("Name:", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(4, row, 60, CONTROL_HEIGHT), win, win_label, NULL);
      window_t *nm = create_window(ps->el->name, 0,
          MAKERECT(68, row, PROPS_W - 72, BUTTON_HEIGHT),
          win, win_textedit, NULL);
      nm->id = PROPS_ID_NAME;

      row += BUTTON_HEIGHT + 6;
      // Type label (read-only info)
      char info[64];
      snprintf(info, sizeof(info), "Type: %s  ID: %d  (%d, %d)  %d × %d",
               ctrl_type_token(ps->el->type), ps->el->id,
               ps->el->x, ps->el->y, ps->el->w, ps->el->h);
      create_window(info, WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(4, row, PROPS_W - 8, CONTROL_HEIGHT),
          win, win_label, (void *)(uintptr_t)kColorTextDisabled);

      // Buttons
      int by = PROPS_H - BUTTON_HEIGHT - 6;
      window_t *ok = create_window("OK", BUTTON_DEFAULT,
          MAKERECT(PROPS_W - 108, by, 50, BUTTON_HEIGHT),
          win, win_button, NULL);
      ok->id = PROPS_ID_OK;
      window_t *ca = create_window("Cancel", 0,
          MAKERECT(PROPS_W - 54, by, 50, BUTTON_HEIGHT),
          win, win_button, NULL);
      ca->id = PROPS_ID_CANCEL;

      return true;
    }

    case kWindowMessageCommand: {
      if (HIWORD(wparam) != kButtonNotificationClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == PROPS_ID_OK) {
        window_t *cap = get_window_item(win, PROPS_ID_CAPTION);
        window_t *nm  = get_window_item(win, PROPS_ID_NAME);
        if (cap) {
          strncpy(ps->el->text, cap->title, sizeof(ps->el->text) - 1);
          ps->el->text[sizeof(ps->el->text) - 1] = '\0';
        }
        if (nm) {
          strncpy(ps->el->name, nm->title, sizeof(ps->el->name) - 1);
          ps->el->name[sizeof(ps->el->name) - 1] = '\0';
        }
        ps->accepted = true;
        end_dialog(win, 1);
        return true;
      }
      if (src->id == PROPS_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

bool show_props_dialog(window_t *parent, form_element_t *el) {
  props_state_t ps = {0};
  ps.el       = el;
  ps.accepted = false;
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  show_dialog("Element Properties",
              MAKERECT((sw - PROPS_W) / 2, (sh - PROPS_H) / 2, PROPS_W, PROPS_H),
              parent, props_proc, &ps);
  return ps.accepted;
}

// ============================================================
// File-picker wrapper (analogous to imageeditor/filepicker.c)
// ============================================================

static bool show_form_file_picker(window_t *parent, bool save_mode,
                                   char *out_path, size_t out_sz) {
  openfilename_t ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = parent;
  ofn.lpstrFile    = out_path;
  ofn.nMaxFile     = (uint32_t)out_sz;
  ofn.lpstrFilter  = "Header Files\0*.h\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags        = save_mode ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
  return save_mode ? get_save_filename(&ofn) : get_open_filename(&ofn);
}

// ============================================================
// Menu command handler
// ============================================================

void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  form_doc_t *doc = g_app->doc;

  switch (id) {
    case ID_FILE_NEW:
      create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
      break;

    case ID_FILE_OPEN: {
      char path[512] = {0};
      window_t *owner = doc ? doc->doc_win : (g_app->menubar_win);
      if (show_form_file_picker(owner, false, path, sizeof(path))) {
        form_doc_t *ndoc = create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
        if (ndoc) {
          if (form_load(ndoc, path)) {
            strncpy(ndoc->filename, path, sizeof(ndoc->filename) - 1);
            ndoc->filename[sizeof(ndoc->filename) - 1] = '\0';
            ndoc->modified = false;
            form_doc_update_title(ndoc);
            if (ndoc->canvas_win) invalidate_window(ndoc->canvas_win);
            send_message(ndoc->doc_win, kWindowMessageStatusBar, 0, path);
          } else {
            send_message(ndoc->doc_win, kWindowMessageStatusBar, 0,
                         (void *)"Failed to load form");
          }
        }
      }
      break;
    }

    case ID_FILE_SAVE:
      if (!doc) break;
      if (!doc->filename[0]) goto do_save_as;
      if (form_save(doc, doc->filename)) {
        doc->modified = false;
        form_doc_update_title(doc);
        send_message(doc->doc_win, kWindowMessageStatusBar, 0, (void *)"Saved");
      } else {
        send_message(doc->doc_win, kWindowMessageStatusBar, 0, (void *)"Save failed");
      }
      break;

    do_save_as:
    case ID_FILE_SAVEAS: {
      if (!doc) break;
      char path[512] = {0};
      if (show_form_file_picker(doc->doc_win, true, path, sizeof(path))) {
        strncpy(doc->filename, path, sizeof(doc->filename) - 1);
        doc->filename[sizeof(doc->filename) - 1] = '\0';
        if (form_save(doc, path)) {
          doc->modified = false;
          form_doc_update_title(doc);
          send_message(doc->doc_win, kWindowMessageStatusBar, 0, path);
        } else {
          send_message(doc->doc_win, kWindowMessageStatusBar, 0, (void *)"Save failed");
        }
      }
      break;
    }

    case ID_FILE_QUIT:
#ifdef BUILD_AS_GEM
      if (g_app) {
        if (doc && doc->doc_win) destroy_window(doc->doc_win);
        if (g_app->tool_win)    destroy_window(g_app->tool_win);
        if (g_app->menubar_win) destroy_window(g_app->menubar_win);
      }
#else
      ui_request_quit();
#endif
      break;

    case ID_EDIT_DELETE: {
      if (!doc) break;
      window_t *cwin = doc->canvas_win;
      if (!cwin) break;
      canvas_state_t *cs = (canvas_state_t *)cwin->userdata;
      if (!cs || cs->selected_idx < 0) break;
      int idx = cs->selected_idx;
      // Remove element by shifting the array
      for (int i = idx; i < doc->element_count - 1; i++)
        doc->elements[i] = doc->elements[i + 1];
      doc->element_count--;
      cs->selected_idx = -1;
      doc->modified = true;
      form_doc_update_title(doc);
      invalidate_window(cwin);
      break;
    }

    case ID_EDIT_PROPS: {
      if (!doc) break;
      window_t *cwin = doc->canvas_win;
      if (!cwin) break;
      canvas_state_t *cs = (canvas_state_t *)cwin->userdata;
      if (!cs || cs->selected_idx < 0) break;
      form_element_t *el = &doc->elements[cs->selected_idx];
      window_t *owner = g_app->menubar_win ? g_app->menubar_win : doc->doc_win;
      if (show_props_dialog(owner, el)) {
        doc->modified = true;
        form_doc_update_title(doc);
        invalidate_window(cwin);
      }
      break;
    }

    case ID_HELP_ABOUT: {
      window_t *owner = g_app->menubar_win ? g_app->menubar_win : (doc ? doc->doc_win : NULL);
      show_about_dialog(owner);
      break;
    }

    // Ignore tool IDs forwarded from the palette (handled in win_toolpalette.c)
    case ID_TOOL_SELECT:
    case ID_TOOL_LABEL:
    case ID_TOOL_TEXTEDIT:
    case ID_TOOL_BUTTON:
    case ID_TOOL_CHECKBOX:
    case ID_TOOL_COMBOBOX:
    case ID_TOOL_LIST:
      break;

    default:
      break;
  }
}

// ============================================================
// Menu bar window procedure
// ============================================================

result_t editor_menubar_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  if (msg == kWindowMessageCommand) {
    uint16_t notif = HIWORD(wparam);
    if (notif == kMenuBarNotificationItemClick ||
        notif == kAcceleratorNotification      ||
        notif == kButtonNotificationClicked) {
      handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  return win_menubar(win, msg, wparam, lparam);
}
