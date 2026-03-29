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
    case kWindowMessageResize:
      // Keep the canvas child window in sync with the resized document window
      // using the framework's resize API instead of direct frame manipulation.
      if (doc && doc->canvas_win)
        resize_window(doc->canvas_win, win->frame.w, win->frame.h);
      return false;
    case kWindowMessageSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
      return false;
    case kWindowMessageClose: {
      // WM_CLOSE analogue: give the user a chance to save before closing.
      // doc_confirm_close() shows a dialog if modified and calls close_document().
      // Return true in all cases — we have handled the close ourselves.
      if (!doc) return false;
      doc_confirm_close(doc, win);
      return true;  // prevent the default show_window(win, false)
    }
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

// Show an "Unsaved Changes" dialog when doc->modified is set.
// If the user chooses Yes, saves the file (if a filename is known).
// If the user chooses Cancel, returns false without closing.
// Otherwise calls close_document() and returns true.
bool doc_confirm_close(canvas_doc_t *doc, window_t *parent_win) {
  if (!doc) return true;
  if (doc->modified) {
    int res = message_box(parent_win,
                          "This image has unsaved changes.\nDo you want to close it?",
                          "Unsaved Changes",
                          MB_YESNOCANCEL);
    if (res == IDCANCEL) return false;
    if (res == IDYES && doc->filename[0])
      png_save(doc->filename, doc);
    // IDNO or IDYES-with-save: fall through to close_document
  }
  close_document(doc);
  return true;
}

canvas_doc_t *create_document(const char *filename, int w, int h) {
  if (!g_app) return NULL;
  if (w <= 0 || h <= 0) return NULL;

  canvas_doc_t *doc = calloc(1, sizeof(canvas_doc_t));
  if (!doc) return NULL;

  doc->canvas_w = w;
  doc->canvas_h = h;
  // Guard against integer overflow in the pixel buffer allocation.
  // Reject images larger than 16384×16384 to keep the size_t arithmetic safe.
  if ((size_t)w > 16384 || (size_t)h > 16384 ||
      (size_t)w * (size_t)h > (size_t)16384 * 16384) {
    free(doc); return NULL;
  }
  doc->pixels = malloc((size_t)w * (size_t)h * 4);
  if (!doc->pixels) { free(doc); return NULL; }

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

  // Compute maximum window size: available screen area to the right of the
  // tool palette and below the menu bar.  Cap the document window so that
  // small images open at their natural size while large images get scrollbars.
  int screen_w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int screen_h = ui_get_system_metrics(kSystemMetricScreenHeight);
  int max_win_w = screen_w - DOC_START_X;
  int max_win_h = screen_h - DOC_START_Y;
  if (max_win_w < 1) max_win_w = 1;
  if (max_win_h < 1) max_win_h = 1;
  int win_w = w < max_win_w ? w : max_win_w;
  int win_h = h < max_win_h ? h : max_win_h;

  // Wrap cascade position when we'd overflow the right edge
  if (g_app->next_x + win_w > screen_w) {
    g_app->next_x = DOC_START_X;
    g_app->next_y = DOC_START_Y;
  }

  window_t *dwin = create_window(
      filename ? filename : "Untitled",
      /* WINDOW_TOOLBAR |*/ WINDOW_STATUSBAR,
      MAKERECT(wx, wy, win_w, win_h),
      NULL, doc_win_proc, NULL);
  dwin->userdata = doc;
  doc->win = dwin;

  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL | WINDOW_VSCROLL,
      MAKERECT(0, 0, win_w, win_h),
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

  image_free(doc->pixels);
  doc->pixels = NULL;

  if (doc->win && is_window(doc->win))
    destroy_window(doc->win);

  free(doc);
}
