// Menu bar, document management, file I/O, and dialog entry points
// for the Orion Form Editor.

#include "formeditor.h"
#include "../../commctl/commctl.h"
#include <inttypes.h>

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
  {"Delete",            ID_EDIT_DELETE},
  {NULL,                0},
  {"Properties...",     ID_EDIT_PROPS},
};

static const menu_item_t kViewItems[] = {
  {"Grid Settings...", ID_VIEW_GRID},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Edit", kEditItems, (int)(sizeof(kEditItems)/sizeof(kEditItems[0]))},
  {"View", kViewItems, (int)(sizeof(kViewItems)/sizeof(kViewItems[0]))},
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
  snprintf(doc->doc_win->title, sizeof(doc->doc_win->title), "%s%s",
           name, doc->modified ? " *" : "");
  invalidate_window(doc->doc_win);
}

// ============================================================
// Document window procedure
// ============================================================

static result_t doc_win_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  form_doc_t *doc = (form_doc_t *)win->userdata;
  switch (msg) {
    case evCreate:
      return true;
    case evPaint:
      fill_rect(get_sys_color(brWorkspaceBg), R(0, 0, win->frame.w, win->frame.h));
      return false;
    case evHScroll:
      // Forward the built-in hscroll notification to the canvas child.
      if (doc && doc->canvas_win)
        send_message(doc->canvas_win, evHScroll, wparam, lparam);
      return true;
    case evResize: {
      if (doc && doc->canvas_win) {
        irect16_t cr = get_client_rect(win);
        resize_window(doc->canvas_win, cr.w, cr.h);
      }
      return false;
    }
    case evClose: {
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

static irect16_t form_doc_frame_for_size(int form_w, int form_h) {
  int max_w = SCREEN_W - DOC_START_X - 4;
  int max_h = SCREEN_H - DOC_START_Y - 4;
  int max_canvas_h = max_h - TITLEBAR_HEIGHT - STATUSBAR_HEIGHT;
  bool needs_vscroll;
  int frame_w;
  int frame_h;

  if (max_w < 1) max_w = 1;
  if (max_canvas_h < 1) max_canvas_h = 1;

  needs_vscroll = form_h > max_canvas_h;
  frame_w = form_w + (needs_vscroll ? SCROLLBAR_WIDTH : 0);
  if (frame_w > max_w) frame_w = max_w;

  frame_h = TITLEBAR_HEIGHT + STATUSBAR_HEIGHT + form_h;
  if (frame_h > max_h) frame_h = max_h;

  return (irect16_t){DOC_START_X, DOC_START_Y, frame_w, frame_h};
}

form_doc_t *create_form_doc(int w, int h) {
  if (!g_app) return NULL;
  if (w <= 0 || h <= 0 || w > INT16_MAX || h > INT16_MAX) return NULL;

  // Close existing document first (single-document editor).
  if (g_app->doc)
    close_form_doc(g_app->doc);

  form_doc_t *doc = (form_doc_t *)calloc(1, sizeof(form_doc_t));
  if (!doc) return NULL;

  doc->form_size.w    = w;
  doc->form_size.h    = h;
  doc->modified  = false;
  doc->next_id   = CTRL_ID_BASE;
  doc->grid_size    = 8;
  doc->show_grid    = true;
  doc->snap_to_grid = true;

  // Document window
  irect16_t doc_frame = form_doc_frame_for_size(w, h);
  window_t *dwin = create_window(
      "Untitled",
      WINDOW_STATUSBAR | WINDOW_HSCROLL,
      &doc_frame,
      NULL, doc_win_proc, g_app->hinstance, NULL);
  dwin->userdata = doc;
  doc->doc_win   = dwin;

  // Canvas child window (owns the VSCROLL) — sized to the document window's client area
  irect16_t cr = get_client_rect(dwin);
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
      MAKERECT(0, 0, cr.w, cr.h),
      dwin, win_canvas_proc, 0, doc);
  cwin->notabstop = false;
  doc->canvas_win = cwin;

  g_app->doc = doc;

  show_window(dwin, true);
  form_doc_update_title(doc);
  send_message(dwin, evStatusBar, 0, (void *)"New form");
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

// Sanitize a string for embedding in a C string literal: escapes '\\', '"',
// '\n', '\r', and '\t' so round-tripping through the form editor is lossless.
static void sanitize_c_str_literal(const char *src, char *dst, size_t dst_sz) {
  size_t di = 0;
  if (dst_sz == 0) return;
  for (size_t si = 0; src[si]; si++) {
    char ch = src[si];
    const char *esc = NULL;
    switch (ch) {
      case '\\': esc = "\\\\"; break;
      case '"':  esc = "\\\""; break;
      case '\n': esc = "\\n";  break;
      case '\r': esc = "\\r";  break;
      case '\t': esc = "\\t";  break;
      default:   break;
    }
    if (esc) {
      if (di + 2 >= dst_sz) break;
      dst[di++] = esc[0];
      dst[di++] = esc[1];
    } else {
      if (di + 1 >= dst_sz) break;
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
      if (di >= ident_sz - 1) { ident[ident_sz - 1] = '\0'; return; }
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

  // Emit #defines for control IDs so the header is usable directly.
  for (int i = 0; i < doc->element_count; i++) {
    form_element_t *el = &doc->elements[i];
    if (is_c_identifier(el->name))
      fprintf(f, "#define %-30s %d\n", el->name, el->id);
  }

  // Emit a form_def_t struct literal that can be passed directly to
  // create_window_from_form() at runtime.
  char ident[64];
  path_to_form_ident(path, ident, sizeof(ident));

  fprintf(f, "\n");
  if (doc->element_count > 0) {
    fprintf(f, "static const form_ctrl_def_t k%s_children[] = {\n", ident);
    for (int i = 0; i < doc->element_count; i++) {
      form_element_t *el = &doc->elements[i];
      char safe_text[sizeof(el->text) * 2];
      char safe_name[sizeof(el->name) * 2];
      sanitize_c_str_literal(el->text, safe_text, sizeof(safe_text));
      sanitize_c_str_literal(el->name, safe_name, sizeof(safe_name));
      fprintf(f, "  { FORM_CTRL_%s, %d, {%d, %d, %d, %d}, %" PRIu32 ", \"%s\", \"%s\" },\n",
              ctrl_type_form_token(el->type), el->id,
              el->frame.x, el->frame.y, el->frame.w, el->frame.h, el->flags,
              safe_text, safe_name);
    }
    fprintf(f, "};\n");
    fprintf(f, "static const form_def_t k%s = {\n", ident);
        fprintf(f, "  .name        = \"%s\",\n", ident);
        fprintf(f, "  .width       = %d,\n", doc->form_size.w);
        fprintf(f, "  .height      = %d,\n", doc->form_size.h);
        fprintf(f, "  .flags       = 0,\n");
        fprintf(f, "  .children    = k%s_children,\n", ident);
        fprintf(f, "  .child_count = (int)(sizeof(k%s_children) / sizeof(k%s_children[0]))\n",
          ident, ident);
    fprintf(f, "};\n");
  } else {
    fprintf(f, "static const form_def_t k%s = {\n", ident);
        fprintf(f, "  .name        = \"%s\",\n", ident);
        fprintf(f, "  .width       = %d,\n", doc->form_size.w);
        fprintf(f, "  .height      = %d,\n", doc->form_size.h);
        fprintf(f, "  .flags       = 0,\n");
        fprintf(f, "  .children    = NULL,\n");
        fprintf(f, "  .child_count = 0\n");
    fprintf(f, "};\n");
  }

  fclose(f);
  return true;
}

// Map FORM_CTRL_XXX token (uppercase) to a ctrl type constant.
static int ctrl_type_from_form_token(const char *tok) {
  if (strcmp(tok, "BUTTON")   == 0) return CTRL_BUTTON;
  if (strcmp(tok, "CHECKBOX") == 0) return CTRL_CHECKBOX;
  if (strcmp(tok, "LABEL")    == 0) return CTRL_LABEL;
  if (strcmp(tok, "TEXTEDIT") == 0) return CTRL_TEXTEDIT;
  if (strcmp(tok, "LIST")     == 0) return CTRL_LIST;
  if (strcmp(tok, "COMBOBOX") == 0) return CTRL_COMBOBOX;
  return -1;
}

// Parse a C string literal starting at *p (pointing at the opening '"').
// Decodes escape sequences produced by sanitize_c_str_literal().
// Advances *p past the closing '"'.  Returns number of chars written (not NUL).
static int parse_c_str_literal(const char **p, char *dst, size_t dst_sz) {
  if (**p != '"') return -1;
  (*p)++;  // skip opening quote
  size_t n = 0;
  while (**p && **p != '"' && n < dst_sz - 1) {
    if (**p == '\\') {
      (*p)++;
      switch (**p) {
        case '"':  dst[n++] = '"';  break;
        case '\\': dst[n++] = '\\'; break;
        case 'n':  dst[n++] = '\n'; break;
        case 'r':  dst[n++] = '\r'; break;
        case 't':  dst[n++] = '\t'; break;
        default:   dst[n++] = **p;  break;  // unknown escape, copy verbatim
      }
    } else {
      dst[n++] = **p;
    }
    (*p)++;
  }
  dst[n] = '\0';
  if (**p == '"') (*p)++;  // skip closing quote
  return (int)n;
}

// Legacy loader: parses the comment-based format produced by older saves.
//   /* ORION_FORM_BEGIN */ ... /* ctrl type id x y w h "text" "name" */ ... /* ORION_FORM_END */
static bool form_load_legacy(form_doc_t *doc, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return false;

  bool found_begin = false;
  bool found_end   = false;
  char line[512];

  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, "ORION_FORM_BEGIN"))  { found_begin = true; continue; }
    if (strstr(line, "ORION_FORM_END"))    { found_end   = true; break;    }
    if (!found_begin) continue;

    // /* form W H */
    {
      int fw = 0, fh = 0;
      if (sscanf(line, " /* form %d %d", &fw, &fh) == 2 &&
          fw > 0 && fh > 0 && fw <= INT16_MAX && fh <= INT16_MAX) {
        doc->form_size.w = fw;
        doc->form_size.h = fh;
        continue;
      }
    }

    // /* ctrl TYPE ID X Y W H "text" "name" */
    if (doc->element_count < MAX_ELEMENTS) {
      char type_tok[32] = {0};
      int id = 0, x = 0, y = 0, w = 0, h = 0;
      char text[64] = {0};
      char name[32] = {0};
      int n = sscanf(line, " /* ctrl %31s %d %d %d %d %d \"%63[^\"]\" \"%31[^\"]",
                     type_tok, &id, &x, &y, &w, &h, text, name);
      if (n >= 6) {
        int type = ctrl_type_from_token(type_tok);
        if (type >= 0 && type < CTRL_TYPE_COUNT) {
          form_element_t *el = &doc->elements[doc->element_count];
          el->type  = type;
          el->id    = id;
          el->frame = (irect16_t){x, y, w > 0 ? w : 10, h > 0 ? h : 8};
          el->flags = 0;
          strncpy(el->text, text, sizeof(el->text) - 1);
          el->text[sizeof(el->text) - 1] = '\0';
          strncpy(el->name, name, sizeof(el->name) - 1);
          el->name[sizeof(el->name) - 1] = '\0';
          doc->element_count++;
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

bool form_load(form_doc_t *doc, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return false;

  // Reset document content.
  doc->element_count = 0;
  doc->form_size.w        = FORM_DEFAULT_W;
  doc->form_size.h        = FORM_DEFAULT_H;
  memset(doc->type_counters, 0, sizeof(doc->type_counters));
  doc->next_id = CTRL_ID_BASE;

  // Single-pass struct-based parser.
  // Recognises:
  //   static const form_ctrl_def_t k<name>_children[] = { ... };
  //   static const form_def_t k<name> = { .width = N, .height = N, ... };
  bool in_children = false;
  bool found_def   = false;
  char line[512];

  while (fgets(line, sizeof(line), f)) {
    // Detect start of children array.
    if (strstr(line, "form_ctrl_def_t k") && strstr(line, "_children[]")) {
      in_children = true;
      continue;
    }
    // Detect end of children array: first non-space chars must be "};" so that
    // control captions containing "};" don't prematurely terminate the block.
    if (in_children) {
      const char *t = line;
      while (*t == ' ' || *t == '\t') t++;
      if (t[0] == '}' && t[1] == ';') {
        in_children = false;
        continue;
      }
    }
    // Parse a child control entry:
    //   { FORM_CTRL_BUTTON, 1001, {10, 10, 75, 23}, 0, "Button1", "IDC_BTN1" },
    if (in_children && doc->element_count < MAX_ELEMENTS) {
      char type_tok[32] = {0};
      int id = 0, x = 0, y = 0, w = 0, h = 0;
      uint32_t flags = 0;
      char text[sizeof(((form_element_t*)0)->text)] = {0};
      char name[sizeof(((form_element_t*)0)->name)] = {0};
      int consumed = 0;
      // Parse the non-string fields; %n records the number of chars consumed.
      int n = sscanf(line,
          " { FORM_CTRL_%31[^,], %d, {%d, %d, %d, %d}, %" SCNu32 "%n",
          type_tok, &id, &x, &y, &w, &h, &flags, &consumed);
      if (n >= 7) {
        // Parse text and name string literals using the full escape decoder.
        const char *p = line + consumed;
        while (*p && *p != '"') p++;
        bool has_text = (*p == '"') && (parse_c_str_literal(&p, text, sizeof(text)) >= 0);
        while (*p && *p != '"') p++;
        bool has_name = (*p == '"') && (parse_c_str_literal(&p, name, sizeof(name)) >= 0);

        int type = ctrl_type_from_form_token(type_tok);
        if (type >= 0 && type < CTRL_TYPE_COUNT) {
          form_element_t *el = &doc->elements[doc->element_count];
          el->type  = type;
          el->id    = id;
          el->frame = (irect16_t){x, y, w > 0 ? w : 10, h > 0 ? h : 8};
          el->flags = flags;
          if (has_text) {
            strncpy(el->text, text, sizeof(el->text) - 1);
            el->text[sizeof(el->text) - 1] = '\0';
          }
          if (has_name) {
            strncpy(el->name, name, sizeof(el->name) - 1);
            el->name[sizeof(el->name) - 1] = '\0';
          }
          doc->element_count++;
          if (id >= doc->next_id) doc->next_id = id + 1;
          doc->type_counters[type]++;
          found_def = true;
        }
      }
    }
    // Parse form dimensions from the form_def_t block.
    int val;
    if (sscanf(line, " .width = %d", &val) == 1 &&
        val > 0 && val <= INT16_MAX) {
      doc->form_size.w = val;
      found_def = true;
    }
    if (sscanf(line, " .height = %d", &val) == 1 &&
        val > 0 && val <= INT16_MAX) {
      doc->form_size.h = val;
      found_def = true;
    }
  }
  fclose(f);

  // If no struct-based data was found, fall back to the old comment format.
  if (!found_def) {
    doc->element_count = 0;
    doc->form_size.w        = FORM_DEFAULT_W;
    doc->form_size.h        = FORM_DEFAULT_H;
    memset(doc->type_counters, 0, sizeof(doc->type_counters));
    doc->next_id = CTRL_ID_BASE;
    return form_load_legacy(doc, path);
  }
  return true;
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
    case evCreate: {
      // Info labels
      create_window("Orion Form Editor", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 8, ABOUT_W - 16, CONTROL_HEIGHT),
          win, win_label, 0, NULL);
      create_window("Version 1.0", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 22, ABOUT_W - 16, CONTROL_HEIGHT),
          win, win_label, 0, (void *)(uintptr_t)brTextDisabled);
      create_window("VB3-inspired form designer", WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(8, 36, ABOUT_W - 16, CONTROL_HEIGHT),
          win, win_label, 0, (void *)(uintptr_t)brTextDisabled);
      // OK button
      create_window("OK", BUTTON_DEFAULT,
          MAKERECT(ABOUT_W - 54, ABOUT_H - BUTTON_HEIGHT - 4, 50, BUTTON_HEIGHT),
          win, win_button, 0, NULL);
      return true;
    }
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        end_dialog(win, 1);
        return true;
      }
      return false;
    default:
      return false;
  }
}

void show_about_dialog(window_t *parent) {
  show_dialog("About Orion Form Editor",
              ABOUT_W, ABOUT_H + TITLEBAR_HEIGHT,
              parent, about_proc, NULL);
}

// ============================================================
// Grid Settings dialog
// ============================================================

#define GRID_W   180
#define GRID_H   108

#define GRID_ROW1_Y   6
#define GRID_ROW2_Y   (GRID_ROW1_Y + BUTTON_HEIGHT + 4)
#define GRID_ROW3_Y   (GRID_ROW2_Y + BUTTON_HEIGHT + 4)
#define GRID_BTN_Y    (GRID_H - BUTTON_HEIGHT - 6)

#define GRID_ID_SHOW   1
#define GRID_ID_SNAP   2
#define GRID_ID_SIZE   3
#define GRID_ID_OK     4
#define GRID_ID_CANCEL 5

#define GRID_SIZE_MIN  1
#define GRID_SIZE_MAX  64

// grid_size is bound via BIND_INT_EDIT; checkboxes are handled manually.
typedef struct {
  int  grid_size;
} grid_size_data_t;

static const form_ctrl_def_t kGridChildren[] = {
  { FORM_CTRL_CHECKBOX, GRID_ID_SHOW, {4,        GRID_ROW1_Y, GRID_W-8, BUTTON_HEIGHT}, 0,             "Show grid",    "chk_show"   },
  { FORM_CTRL_CHECKBOX, GRID_ID_SNAP, {4,        GRID_ROW2_Y, GRID_W-8, BUTTON_HEIGHT}, 0,             "Snap to grid", "chk_snap"   },
  { FORM_CTRL_LABEL,    -1,           {4,        GRID_ROW3_Y, 60,       CONTROL_HEIGHT}, 0,             "Grid size:",   "lbl_size"   },
  { FORM_CTRL_TEXTEDIT, GRID_ID_SIZE, {68,       GRID_ROW3_Y, 40,       BUTTON_HEIGHT},  0,             "",             "edit_size"  },
  { FORM_CTRL_BUTTON,   GRID_ID_OK,     {GRID_W-108, GRID_BTN_Y, 50, BUTTON_HEIGHT}, BUTTON_DEFAULT, "OK",     "btn_ok"     },
  { FORM_CTRL_BUTTON,   GRID_ID_CANCEL, {GRID_W-54,  GRID_BTN_Y, 50, BUTTON_HEIGHT}, 0,             "Cancel", "btn_cancel" },
};

static const ctrl_binding_t k_grid_bindings[] = {
  { GRID_ID_SIZE, BIND_INT_EDIT, offsetof(grid_size_data_t, grid_size), 0 },
};

static const form_def_t kGridForm = {
  .name          = "Grid Settings",
  .width         = GRID_W,
  .height        = GRID_H,
  .flags         = 0,
  .children      = kGridChildren,
  .child_count   = ARRAY_LEN(kGridChildren),
  .bindings      = k_grid_bindings,
  .binding_count = ARRAY_LEN(k_grid_bindings),
  .ok_id         = GRID_ID_OK,
  .cancel_id     = GRID_ID_CANCEL,
};

typedef struct {
  form_doc_t *doc;
  bool        accepted;
} grid_dlg_state_t;

static result_t grid_dlg_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  grid_dlg_state_t *gs = (grid_dlg_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      gs = (grid_dlg_state_t *)lparam;
      win->userdata = gs;
      form_doc_t *doc = gs->doc;
      // Set checkbox states manually (no BIND_BOOL).
      window_t *chk_show = get_window_item(win, GRID_ID_SHOW);
      window_t *chk_snap = get_window_item(win, GRID_ID_SNAP);
      if (chk_show)
        send_message(chk_show, btnSetCheck,
                     doc->show_grid ? btnStateChecked : btnStateUnchecked, NULL);
      if (chk_snap)
        send_message(chk_snap, btnSetCheck,
                     doc->snap_to_grid ? btnStateChecked : btnStateUnchecked, NULL);
      // Push grid_size via DDX.
      grid_size_data_t gsd = { doc->grid_size };
      dialog_push(win, &gsd, k_grid_bindings, ARRAY_LEN(k_grid_bindings));
      return true;
    }
    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;
      if (src->id == GRID_ID_OK) {
        form_doc_t *doc = gs->doc;
        // Pull grid_size via DDX.
        grid_size_data_t gsd = { doc->grid_size };
        dialog_pull(win, &gsd, k_grid_bindings, ARRAY_LEN(k_grid_bindings));
        if (gsd.grid_size < GRID_SIZE_MIN) gsd.grid_size = GRID_SIZE_MIN;
        if (gsd.grid_size > GRID_SIZE_MAX) gsd.grid_size = GRID_SIZE_MAX;
        doc->grid_size = gsd.grid_size;
        // Pull checkboxes manually.
        window_t *chk_show = get_window_item(win, GRID_ID_SHOW);
        window_t *chk_snap = get_window_item(win, GRID_ID_SNAP);
        if (chk_show)
          doc->show_grid = (send_message(chk_show, btnGetCheck, 0, NULL) == btnStateChecked);
        if (chk_snap)
          doc->snap_to_grid = (send_message(chk_snap, btnGetCheck, 0, NULL) == btnStateChecked);
        gs->accepted = true;
        end_dialog(win, 1);
        return true;
      }
      if (src->id == GRID_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

static void show_grid_settings_dialog(window_t *parent, form_doc_t *doc) {
  grid_dlg_state_t gs = { doc, false };
  show_dialog_from_form(&kGridForm, "Grid Settings", parent, grid_dlg_proc, &gs);
  if (gs.accepted && doc->canvas_win)
    invalidate_window(doc->canvas_win);
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

// Computed row positions (mirrors the form below)
#define PROPS_ROW1_Y       4
#define PROPS_ROW2_Y       (PROPS_ROW1_Y + BUTTON_HEIGHT + 6)   // 23
#define PROPS_INFO_Y       (PROPS_ROW2_Y + BUTTON_HEIGHT + 6)   // 42
#define PROPS_BTN_Y        (PROPS_H - BUTTON_HEIGHT - 6)        // 86

static const form_ctrl_def_t kPropsChildren[] = {
  { FORM_CTRL_LABEL,    -1,              {4,          PROPS_ROW1_Y, 60,           CONTROL_HEIGHT}, 0,             "Caption:", "lbl_caption" },
  { FORM_CTRL_TEXTEDIT, PROPS_ID_CAPTION,{68,         PROPS_ROW1_Y, PROPS_W - 72, BUTTON_HEIGHT},  0,             "",         "edit_caption"},
  { FORM_CTRL_LABEL,    -1,              {4,          PROPS_ROW2_Y, 60,           CONTROL_HEIGHT}, 0,             "Name:",    "lbl_name"    },
  { FORM_CTRL_TEXTEDIT, PROPS_ID_NAME,   {68,         PROPS_ROW2_Y, PROPS_W - 72, BUTTON_HEIGHT},  0,             "",         "edit_name"   },
  { FORM_CTRL_BUTTON,   PROPS_ID_OK,     {PROPS_W-108, PROPS_BTN_Y, 50,           BUTTON_HEIGHT},  BUTTON_DEFAULT, "OK",      "btn_ok"      },
  { FORM_CTRL_BUTTON,   PROPS_ID_CANCEL, {PROPS_W-54,  PROPS_BTN_Y, 50,           BUTTON_HEIGHT},  0,             "Cancel",   "btn_cancel"  },
};

// DDX bindings: caption and name edits ↔ form_element_t.text / .name
static const ctrl_binding_t k_props_bindings[] = {
  { PROPS_ID_CAPTION, BIND_STRING, offsetof(form_element_t, text), sizeof_field(form_element_t, text) },
  { PROPS_ID_NAME,    BIND_STRING, offsetof(form_element_t, name), sizeof_field(form_element_t, name) },
};

static const form_def_t kPropsForm = {
  .name          = "Element Properties",
  .width         = PROPS_W,
  .height        = PROPS_H,
  .flags         = 0,
  .children      = kPropsChildren,
  .child_count   = ARRAY_LEN(kPropsChildren),
  .bindings      = k_props_bindings,
  .binding_count = ARRAY_LEN(k_props_bindings),
  .ok_id         = PROPS_ID_OK,
  .cancel_id     = PROPS_ID_CANCEL,
};

typedef struct {
  form_element_t *el;
  bool            accepted;
} props_state_t;

static result_t props_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  props_state_t *ps = (props_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      ps = (props_state_t *)lparam;
      win->userdata = ps;

      // Dynamic type-info label (content is computed at runtime).
      char info[64];
      snprintf(info, sizeof(info), "Type: %s  ID: %d  (%d, %d)  %d x %d",
               ctrl_type_token(ps->el->type), ps->el->id,
               ps->el->frame.x, ps->el->frame.y, ps->el->frame.w, ps->el->frame.h);
      create_window(info, WINDOW_NOTITLE | WINDOW_NOFILL,
          MAKERECT(4, PROPS_INFO_Y, PROPS_W - 8, CONTROL_HEIGHT),
          win, win_label, 0, (void *)(uintptr_t)brTextDisabled);

      // Pre-populate caption/name edits from the element.
      dialog_push(win, ps->el, k_props_bindings, ARRAY_LEN(k_props_bindings));
      return true;
    }

    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == PROPS_ID_OK) {
        dialog_pull(win, ps->el, k_props_bindings, ARRAY_LEN(k_props_bindings));
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
  show_dialog_from_form(&kPropsForm, "Element Properties", parent, props_proc, &ps);
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
            canvas_rebuild_live_controls(ndoc);
            send_message(ndoc->doc_win, evStatusBar, 0, path);
          } else {
            send_message(ndoc->doc_win, evStatusBar, 0,
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
        send_message(doc->doc_win, evStatusBar, 0, (void *)"Saved");
      } else {
        send_message(doc->doc_win, evStatusBar, 0, (void *)"Save failed");
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
          send_message(doc->doc_win, evStatusBar, 0, path);
        } else {
          send_message(doc->doc_win, evStatusBar, 0, (void *)"Save failed");
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
      if (doc->elements[idx].live_win)
        destroy_window(doc->elements[idx].live_win);
      // Remove element by shifting the array
      for (int i = idx; i < doc->element_count - 1; i++)
        doc->elements[i] = doc->elements[i + 1];
      doc->element_count--;
      cs->selected_idx = -1;
      doc->modified = true;
      form_doc_update_title(doc);
      canvas_rebuild_live_controls(doc);
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
        canvas_sync_live_controls(doc);
      }
      break;
    }

    case ID_VIEW_GRID: {
      if (!doc) break;
      window_t *owner = g_app->menubar_win ? g_app->menubar_win : doc->doc_win;
      show_grid_settings_dialog(owner, doc);
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
  if (msg == evCommand) {
    uint16_t notif = HIWORD(wparam);
    if (notif == kMenuBarNotificationItemClick ||
        notif == kAcceleratorNotification      ||
        notif == btnClicked) {
      handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  return win_menubar(win, msg, wparam, lparam);
}
