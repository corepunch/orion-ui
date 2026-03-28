// Document window proc and document management

#include "imageeditor.h"

// ============================================================
// Document window proc
// ============================================================

static result_t doc_win_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  canvas_doc_t *doc = (canvas_doc_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      return true;
    case kWindowMessagePaint:
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      return false;
    case kWindowMessageSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
      return false;
    default:
      return false;
  }
}

// ============================================================
// Document title helper
// ============================================================

void doc_update_title(canvas_doc_t *doc) {
  if (!doc->win) return;
  char title[64];
  const char *name = doc->filename[0] ? doc->filename : "Untitled";
  const char *slash = strrchr(name, '/');
  if (slash) name = slash + 1;
  snprintf(title, sizeof(title), "%s%s", name, doc->modified ? " *" : "");
  strncpy(doc->win->title, title, sizeof(doc->win->title) - 1);
  doc->win->title[sizeof(doc->win->title) - 1] = '\0';
  invalidate_window(doc->win);
}

// ============================================================
// Document management
// ============================================================

canvas_doc_t *create_document(const char *filename) {
  if (!g_app) return NULL;

  canvas_doc_t *doc = calloc(1, sizeof(canvas_doc_t));
  if (!doc) return NULL;
  canvas_clear(doc);
  doc->modified = false;
  if (filename) {
    strncpy(doc->filename, filename, sizeof(doc->filename) - 1);
    doc->filename[sizeof(doc->filename) - 1] = '\0';
  }

  int wx = g_app->next_x;
  int wy = g_app->next_y;
  g_app->next_x += DOC_CASCADE;
  g_app->next_y += DOC_CASCADE;
  if (g_app->next_x + CANVAS_W > SCREEN_W) {
    g_app->next_x = DOC_START_X;
    g_app->next_y = DOC_START_Y;
  }

  window_t *dwin = create_window(
      filename ? filename : "Untitled",
      /* WINDOW_TOOLBAR |*/ WINDOW_STATUSBAR,
      MAKERECT(wx, wy, CANVAS_W, CANVAS_H),
      NULL, doc_win_proc, NULL);
  dwin->userdata = doc;
  doc->win = dwin;

  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL,
      MAKERECT(0, 0, CANVAS_W, CANVAS_H),
      dwin, win_canvas_proc, doc);
  cwin->notabstop = false;
  doc->canvas_win = cwin;

  show_window(dwin, true);

  doc->next   = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;

  doc_update_title(doc);
  send_message(dwin, kWindowMessageStatusBar, 0,
               (void *)(filename ? filename : "New image"));
  return doc;
}

void close_document(canvas_doc_t *doc) {
  if (!doc || !g_app) return;

  if (g_app->active_doc == doc)
    g_app->active_doc = NULL;

  if (g_app->docs == doc) {
    g_app->docs = doc->next;
  } else {
    for (canvas_doc_t *d = g_app->docs; d; d = d->next) {
      if (d->next == doc) { d->next = doc->next; break; }
    }
  }

  doc_free_undo(doc);

  free(doc->shape_snapshot);
  doc->shape_snapshot = NULL;
  if (doc->float_tex)
    glDeleteTextures(1, &doc->float_tex);
  free(doc->float_pixels);

  if (doc->canvas_tex)
    glDeleteTextures(1, &doc->canvas_tex);

  if (doc->win && is_window(doc->win))
    destroy_window(doc->win);

  free(doc);
}
