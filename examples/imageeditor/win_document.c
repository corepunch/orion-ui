// Document window proc and document management

#include "imageeditor.h"

irect16_t imageeditor_document_workspace_rect(void) {
  int screen_w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int screen_h = ui_get_system_metrics(kSystemMetricScreenHeight);
  if (screen_w <= 0) screen_w = SCREEN_W;
  if (screen_h <= 0) screen_h = SCREEN_H;

  int left_palette_right = PALETTE_WIN_X + PALETTE_WIN_W;
  int tool_opts_right = TOOL_OPTIONS_WIN_X + TOOL_OPTIONS_WIN_W;
  int left = MAX(DOC_START_X,
                 MAX(left_palette_right, tool_opts_right) + DOC_PALETTE_GAP);

  int right_palette_left = MIN(COLOR_WIN_X, LAYERS_WIN_X);
  int right = MIN(screen_w - DOC_WORKSPACE_MARGIN,
                  right_palette_left - DOC_WORKSPACE_MARGIN);

  int top = MAX(DOC_START_Y, APP_TOOLBAR_Y + APP_TOOLBAR_H + DOC_PALETTE_GAP);
  int bottom = screen_h - DOC_WORKSPACE_MARGIN;

  if (right <= left) right = left + 1;
  if (bottom <= top) bottom = top + 1;

  return (irect16_t){ left, top, right - left, bottom - top };
}

void imageeditor_max_document_frame_size(int *out_w, int *out_h) {
  irect16_t ws = imageeditor_document_workspace_rect();
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

void imageeditor_document_frame_for_viewport(int viewport_w, int viewport_h,
                                             int *out_w, int *out_h) {
  int max_frame_w = 1;
  int max_frame_h = 1;
  imageeditor_max_document_frame_size(&max_frame_w, &max_frame_h);

  int frame_w = MAX(1, viewport_w) + SCROLLBAR_WIDTH;
  int frame_h = MAX(1, viewport_h) + TITLEBAR_HEIGHT + STATUSBAR_HEIGHT;

  if (out_w) *out_w = MIN(frame_w, max_frame_w);
  if (out_h) *out_h = MIN(frame_h, max_frame_h);
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
      irect16_t cr = get_client_rect(win);
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
  doc->background.color = MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF);
  doc->background.show = true;
  // Guard against integer overflow in the pixel buffer allocation.
  // Reject images larger than 16384x16384 to keep the size_t arithmetic safe.
  if ((size_t)w > 16384 || (size_t)h > 16384 ||
      (size_t)w * (size_t)h > (size_t)16384 * 16384) {
    free(doc); return NULL;
  }

  // Allocate the composite scratch buffer.
  doc->layer.composite_buf = malloc((size_t)w * (size_t)h * 4);
  if (!doc->layer.composite_buf) { free(doc); return NULL; }

  // Add the initial transparent layer (doc_add_layer also sets doc->pixels).
  if (!doc_add_layer(doc)) {
    free(doc->layer.composite_buf);
    free(doc);
    return NULL;
  }

  canvas_clear(doc);
  doc->modified = false;

  // Always initialize the animation timeline with one frame capturing the
  // current canvas pixels.  Single-canvas workflows simply use frame 0.
  doc->anim = anim_timeline_new(w, h);
  if (doc->anim)
    anim_frame_compress(doc->anim->frames[0], doc->pixels, w, h,
                        FRAME_FORMAT_RGBA);

  if (filename) {
    strncpy(doc->filename, filename, sizeof(doc->filename) - 1);
    doc->filename[sizeof(doc->filename) - 1] = '\0';
  }

  int max_view_w = 1;
  int max_view_h = 1;
  imageeditor_max_canvas_viewport_size(&max_view_w, &max_view_h);
  int viewport_w = MIN(w, max_view_w);
  int viewport_h = MIN(h, max_view_h);
  int win_w = 1;
  int win_h = 1;
  imageeditor_document_frame_for_viewport(viewport_w, viewport_h, &win_w, &win_h);
  irect16_t ws = imageeditor_document_workspace_rect();
  set_default_window_position(ws.x, ws.y);

  window_t *dwin = create_window(
      filename ? filename : "Untitled",
      WINDOW_STATUSBAR | WINDOW_HSCROLL,
      MAKERECT(CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h),
      NULL, doc_win_proc, g_app->hinstance, NULL);
  dwin->userdata = doc;
  doc->win = dwin;

  // Canvas child fills the document window's client area.
  irect16_t cr = get_client_rect(dwin);
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
      MAKERECT(0, 0, cr.w, cr.h),
      dwin, win_canvas_proc, 0, doc);
  cwin->flags &= ~WINDOW_NOTABSTOP;
  doc->canvas_win = cwin;

  show_window(dwin, true);

  doc->next   = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;
  if (!g_app->main_toolbar_win)
    create_main_toolbar_window();
  imageeditor_sync_main_toolbar();

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

  free(doc->shape.snapshot);
  doc->shape.snapshot = NULL;
  if (doc->sel.floating.tex)
    glDeleteTextures(1, &doc->sel.floating.tex);
  free(doc->sel.floating.pixels);
  free(doc->sel.floating.mask);
  canvas_clear_selection_mask(doc);

  doc_free_layers(doc);
  free(doc->layer.composite_buf);
  doc->layer.composite_buf = NULL;

  if (doc->anim) {
    anim_timeline_free(doc->anim);
    doc->anim = NULL;
  }

  if (doc->win && is_window(doc->win))
    destroy_window(doc->win);

  free(doc);

  // Rebuild the Window menu to remove the closed document.
  window_menu_rebuild();
}
