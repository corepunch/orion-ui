// Document window proc and document management

#include "imageeditor.h"

rect_t imageeditor_document_workspace_rect(void) {
  int screen_w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int screen_h = ui_get_system_metrics(kSystemMetricScreenHeight);

  int left_palette_right = PALETTE_WIN_X + PALETTE_WIN_W;
  int tool_opts_right = TOOL_OPTIONS_WIN_X + TOOL_OPTIONS_WIN_W;
  int left = MAX(DOC_START_X,
                 MAX(left_palette_right, tool_opts_right) + DOC_WORKSPACE_MARGIN);

  int right_palette_left = MIN(COLOR_WIN_X, LAYERS_WIN_X);
  int right = MIN(screen_w - DOC_WORKSPACE_MARGIN,
                  right_palette_left - DOC_WORKSPACE_MARGIN);

  int top = MAX(DOC_START_Y, MENUBAR_HEIGHT + DOC_WORKSPACE_MARGIN);
  int bottom = screen_h - DOC_WORKSPACE_MARGIN;

  if (right <= left) right = left + 1;
  if (bottom <= top) bottom = top + 1;

  return (rect_t){ left, top, right - left, bottom - top };
}

void imageeditor_max_document_frame_size(int *out_w, int *out_h) {
  rect_t ws = imageeditor_document_workspace_rect();
  if (out_w) *out_w = MAX(1, ws.w);
  if (out_h) *out_h = MAX(1, ws.h);
}

void imageeditor_max_canvas_viewport_size(int *out_w, int *out_h) {
  int frame_w = 1;
  int frame_h = 1;
  imageeditor_max_document_frame_size(&frame_w, &frame_h);
  if (out_w) *out_w = MAX(1, frame_w - SCROLLBAR_WIDTH);
  if (out_h) *out_h = MAX(1, frame_h - TITLEBAR_HEIGHT - STATUSBAR_HEIGHT);
}

// ============================================================
// Document window proc
// ============================================================

static result_t doc_win_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  canvas_doc_t *doc = (canvas_doc_t *)win->userdata;
  switch (msg) {
    case evHScroll:
      // Forward horizontal-scroll notifications from the doc window's built-in
      // hscroll (which is merged with the status bar) to the canvas child.
      if (doc && doc->canvas_win)
        send_message(doc->canvas_win, evHScroll, wparam, lparam);
      return true;
    case evCreate:
      return true;
    case evPaint:
      fill_rect(get_sys_color(brWindowDarkBg), R(0, 0, win->frame.w, win->frame.h));
      return false;
    case evResize: {
      // Keep the canvas child window in sync with the document window's client area.
      rect_t cr = get_client_rect(win);
      if (doc && doc->canvas_win)
        resize_window(doc->canvas_win, cr.w, cr.h);
      return false;
    }
    case evSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
      return false;
    case evClose: {
      // WM_CLOSE analogue: give the user a chance to save before closing.
      // doc_confirm_close() shows a dialog if modified and calls close_document().
      // Return true in all cases — we have handled the close ourselves.
      if (!doc) return false;
      debug_log_doc_state("wm_close", doc);
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
  const char *name = doc->filename[0] ? doc->filename : "Untitled";
  const char *slash = strrchr(name, '/');
  if (slash) name = slash + 1;
  snprintf(doc->win->title, sizeof(doc->win->title), "%s%s",
           name, doc->modified ? " *" : "");
  invalidate_window(doc->win);
}

// ============================================================
// Document management
// ============================================================

// Show an "Unsaved Changes" dialog when doc->modified is set.
// If the user chooses Yes, saves the file (if a filename is known).
// Otherwise calls close_document() and returns true.
bool doc_confirm_close(canvas_doc_t *doc, window_t *parent_win) {
  if (!doc) return true;
  if (doc->close_prompt_open) {
    debug_log_doc_state("close_confirm_reentered", doc);
    return false;
  }
  if (doc->modified) {
    debug_log_doc_state("close_confirm_open", doc);
    doc->close_prompt_open = true;
    int res = message_box(parent_win,
                          "This image has unsaved changes.\nDo you want to close it?",
                          "Unsaved Changes",
                          MB_YESNO);
    doc->close_prompt_open = false;
    IE_DEBUG("close_confirm_result doc=%p result=%d filename_set=%d",
             (void *)doc, res, doc->filename[0] != '\0');
    if (res == IDYES && doc->filename[0])
      png_save(doc->filename, doc);
    // IDNO or IDYES-with-save: fall through to close_document
  }
  debug_log_doc_state("close_confirm_accept", doc);
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
  // Reject images larger than 16384x16384 to keep the size_t arithmetic safe.
  if ((size_t)w > 16384 || (size_t)h > 16384 ||
      (size_t)w * (size_t)h > (size_t)16384 * 16384) {
    free(doc); return NULL;
  }

  // Allocate the composite scratch buffer.
  doc->composite_buf = malloc((size_t)w * (size_t)h * 4);
  if (!doc->composite_buf) { free(doc); return NULL; }

  // Add the initial background layer (doc_add_layer also sets doc->pixels).
  if (!doc_add_layer(doc)) {
    free(doc->composite_buf);
    free(doc);
    return NULL;
  }

  canvas_clear(doc);
  doc->modified = false;
  if (filename) {
    strncpy(doc->filename, filename, sizeof(doc->filename) - 1);
    doc->filename[sizeof(doc->filename) - 1] = '\0';
  }

  rect_t ws = imageeditor_document_workspace_rect();
  int wx = g_app->next_x;
  int wy = g_app->next_y;
  g_app->next_x += DOC_CASCADE;
  g_app->next_y += DOC_CASCADE;

  int max_win_w = MAX(1, ws.w);
  int max_win_h = MAX(1, ws.h);
  int max_canvas_h = MAX(1, max_win_h - TITLEBAR_HEIGHT - STATUSBAR_HEIGHT);
  int win_w = MIN(w, max_win_w);
  int win_h = MIN(h, max_canvas_h) + TITLEBAR_HEIGHT + STATUSBAR_HEIGHT;

  // Keep the cascade inside the usable center workspace.
  if (wx < ws.x || wy < ws.y || wx + win_w > ws.x + ws.w || wy + win_h > ws.y + ws.h) {
    wx = ws.x;
    wy = ws.y;
  }
  if (g_app->next_x + win_w > ws.x + ws.w || g_app->next_y + win_h > ws.y + ws.h) {
    g_app->next_x = ws.x;
    g_app->next_y = ws.y;
  }

  window_t *dwin = create_window(
      filename ? filename : "Untitled",
      /* WINDOW_TOOLBAR |*/ WINDOW_STATUSBAR | WINDOW_HSCROLL,
      MAKERECT(wx, wy, win_w, win_h),
      NULL, doc_win_proc, g_app->hinstance, NULL);
  dwin->userdata = doc;
  doc->win = dwin;

  // Canvas child fills the document window's client area.
  rect_t cr = get_client_rect(dwin);
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
      MAKERECT(0, 0, cr.w, cr.h),
      dwin, win_canvas_proc, 0, doc);
  cwin->notabstop = false;
  doc->canvas_win = cwin;

  show_window(dwin, true);

  doc->next   = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;

  doc_update_title(doc);
  send_message(dwin, evStatusBar, 0,
               (void *)(filename ? filename : "New image"));

  // Rebuild the Window menu so the new document appears in the list.
  window_menu_rebuild();

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

  doc_free_layers(doc);
  free(doc->composite_buf);
  doc->composite_buf = NULL;

  if (doc->win && is_window(doc->win))
    destroy_window(doc->win);

  free(doc);

  // Rebuild the Window menu to remove the closed document.
  window_menu_rebuild();
}
