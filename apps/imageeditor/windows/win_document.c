// Document window proc and document management

#include "imageeditor.h"

#ifdef AX_PLATFORM_IOS
static void imageeditor_layout_ipad_palettes(void) {
  if (!g_app) return;
  irect16_t screen = R(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth),
                                   ui_get_system_metrics(kSystemMetricScreenHeight));
  irect16_t area = rect_inset(rect_trim_top(screen, APP_TOOLBAR_Y + APP_TOOLBAR_H), 4);
  irect16_t right = rect_split_right(area, RIGHT_PANE_WIN_W);
  window_t *panels[] = {g_app->color_win, g_app->layers_win};
  for (size_t i = 0; i < ARRAY_LEN(panels); i++) {
    if (!panels[i]) continue;
    irect16_t frame = rect_split_top(right, MIN(panels[i]->frame.h, right.h));
    move_window(panels[i], frame.x, frame.y);
    resize_window(panels[i], frame.w, frame.h);
    right = rect_trim_top(right, frame.h + 4);
  }
}
#endif

irect16_t imageeditor_document_workspace_rect(void) {
  int screen_w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int screen_h = ui_get_system_metrics(kSystemMetricScreenHeight);
  if (screen_w <= 0) screen_w = SCREEN_W;
  if (screen_h <= 0) screen_h = SCREEN_H;

#if IMAGEEDITOR_BW
  int left = g_app && g_app->tool_win ? window_screen_x(g_app->tool_win) + g_app->tool_win->frame.w : PALETTE_WIN_W;
  int top = g_app && g_app->main_toolbar_win ? window_screen_y(g_app->main_toolbar_win) + g_app->main_toolbar_win->frame.h
                                             : MENUBAR_HEIGHT + APP_TOOLBAR_H;
  return rect_trim_left(rect_trim_top(R(0, 0, screen_w, screen_h), top), left);
#elif defined(AX_PLATFORM_IOS)
  irect16_t area = rect_trim_top(R(0, 0, screen_w, screen_h), APP_TOOLBAR_Y + APP_TOOLBAR_H);
  return rect_inset(rect_trim_right(rect_trim_left(area, PALETTE_WIN_W + 8), RIGHT_PANE_WIN_W + 8), 4);
#else
  int left_palette_right = PALETTE_WIN_X + PALETTE_WIN_W;
  int left = MAX(DOC_START_X, left_palette_right + DOC_PALETTE_GAP);

  int right_palette_left = MIN(COLOR_WIN_X, LAYERS_WIN_X);
  int right = MIN(screen_w - DOC_WORKSPACE_MARGIN,
                  right_palette_left - DOC_WORKSPACE_MARGIN);

  int top = MAX(DOC_START_Y, APP_TOOLBAR_Y + APP_TOOLBAR_H + DOC_PALETTE_GAP);
  int bottom = screen_h - DOC_WORKSPACE_MARGIN;

  if (right <= left) right = left + 1;
  if (bottom <= top) bottom = top + 1;

  return (irect16_t){ left, top, right - left, bottom - top };
#endif
}

void imageeditor_max_document_frame_size(int *out_w, int *out_h) {
  irect16_t ws = imageeditor_document_workspace_rect();
  if (out_w) *out_w = MAX(1, ws.w);
  if (out_h) *out_h = MAX(1, ws.h);
}

void imageeditor_max_canvas_viewport_size(int *out_w, int *out_h) {
  int frame_w = 1;
  int frame_h = 1;
  int status_h = IMAGEEDITOR_DOCUMENT_STATUS_H;
  imageeditor_max_document_frame_size(&frame_w, &frame_h);
  if (out_w) *out_w = MAX(1, frame_w);
  if (out_h) *out_h = MAX(1, frame_h - TITLEBAR_HEIGHT - status_h);
}

void imageeditor_default_canvas_size(int *out_w, int *out_h) {
  irect16_t canvas = imageeditor_document_workspace_rect();
  if (out_w) *out_w = MAX(1, canvas.w);
  if (out_h) *out_h = MAX(1, canvas.h);
}

void imageeditor_document_frame_for_viewport(int viewport_w, int viewport_h,
                                             int *out_w, int *out_h) {
  int max_frame_w = 1;
  int max_frame_h = 1;
  int status_h = IMAGEEDITOR_DOCUMENT_STATUS_H;
  imageeditor_max_document_frame_size(&max_frame_w, &max_frame_h);

  int frame_w = MAX(1, viewport_w);
  int frame_h = MAX(1, viewport_h) + TITLEBAR_HEIGHT + status_h;

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
    case evGetWorkspaceRect:
      if (lparam) *(irect16_t *)lparam = imageeditor_document_workspace_rect();
      return true;
#ifdef AX_PLATFORM_IOS
    case evDisplayChange: {
      if (doc == g_app->active_doc) imageeditor_layout_ipad_palettes();
      return true;
    }
#endif
    case evHScroll:
      // Forward horizontal-scroll notifications from the doc window's built-in
      // hscroll (which is merged with the status bar) to the canvas child.
      if (doc && doc->canvas_win)
        send_message(doc->canvas_win, evHScroll, wparam, lparam);
      return true;
    case evVScroll:
      // The document window owns both built-in scrollbars so the vertical bar
      // stays fixed to the document frame while the canvas viewport pans.
      if (doc && doc->canvas_win)
        send_message(doc->canvas_win, evVScroll, wparam, lparam);
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
      if (g_app && doc) { g_app->active_doc = doc; imageeditor_sync_tool_palette(); }
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
// If the user chooses Yes, closes the document without saving.
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
    // IDNO: cancel close, keep document open with unsaved changes
    if (res == IDNO) {
      debug_log_doc_state("close_confirm_cancelled", doc);
      return false;
    }
    // IDYES: proceed to close without saving
  }
  debug_log_doc_state("close_confirm_accept", doc);
  close_document(doc);
  return true;
}

canvas_doc_t *create_document(const char *filename, int w, int h) {
  int scale = MAX(1, g_bw_retina_scale);
  if (w <= 0 || h <= 0 || w > 16384 / scale || h > 16384 / scale) return NULL;
  return create_document_pixels(filename, w * scale, h * scale);
}

canvas_doc_t *create_document_pixels(const char *filename, int w, int h) {
  if (!g_app) return NULL;

  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return NULL;

  int buf_w = w;
  int buf_h = h;
  w = MAX(1, w / MAX(1, g_bw_retina_scale));
  h = MAX(1, h / MAX(1, g_bw_retina_scale));

  canvas_doc_t *doc = calloc(1, sizeof(canvas_doc_t));
  if (!doc) return NULL;

  doc->canvas_w = buf_w;
  doc->canvas_h = buf_h;
  doc->background.color = IE_PAPER_COLOR;
  doc->background.show = true;
  // Guard against integer overflow in the pixel buffer allocation.
  // Reject images larger than 16384x16384 to keep the size_t arithmetic safe.
  if ((size_t)buf_w > 16384 || (size_t)buf_h > 16384 ||
      (size_t)buf_w * (size_t)buf_h > (size_t)16384 * 16384) {
    free(doc); return NULL;
  }

  // Allocate the composite scratch buffer.
  doc->layer.composite_buf = malloc((size_t)buf_w * (size_t)buf_h * 4);
  if (!doc->layer.composite_buf) { free(doc); return NULL; }

  // Add the initial transparent layer (doc_add_layer also sets doc->pixels).
  if (!doc_add_layer(doc)) {
    free(doc->layer.composite_buf);
    free(doc);
    return NULL;
  }

#if IMAGEEDITOR_BW
  static const char *const names[] = {"Background", "Color", "Pencil", "FX"};
  while (doc->layer.count < IE_LAYER_COUNT) {
    if (!doc_add_layer(doc)) { doc_free_layers(doc); free(doc->layer.composite_buf); free(doc); return NULL; }
  }
  for (int i = 0; i < IE_LAYER_COUNT; i++)
    snprintf(doc->layer.stack[i]->name, sizeof(doc->layer.stack[i]->name), "%s", names[i]);
  doc_set_active_layer(doc, IE_LAYER_PENCIL);
#endif

  canvas_clear(doc);
  doc->modified = false;

#if IMAGEEDITOR_INDEXED
  // Initialize the palette.
#if IMAGEEDITOR_BW
  doc->ipal.transparent = 255;
  memcpy(doc->ipal.entries, k_pencil_palette, sizeof(k_pencil_palette));
  doc->ipal.entries[255] = 0;
  doc->ipal.count = IE_PENCIL_COLORS;
  for (int i = 0; i < IE_LAYER_COUNT; i++)
    memset(doc->layer.stack[i]->pixels, i == IE_LAYER_PENCIL ? 0 : i == IE_LAYER_BG ? 1 : 255,
           (size_t)buf_w * buf_h);
#else
  // Full 256-color palette: index 0 is transparent, indices 1–255 cycle through common colors.
  doc->ipal.transparent = IMAGEEDITOR_TRANSPARENT_INDEX;
  doc->ipal.entries[0]  = MAKE_COLOR(0x00, 0x00, 0x00, 0x00); // transparent
  // Indices 1-15: a standard 4-bit EGA-style base palette.
  static const uint32_t kBasePalette[] = {
    MAKE_COLOR(0,0,0,255),       // 1  black
    MAKE_COLOR(170,0,0,255),     // 2  dark red
    MAKE_COLOR(0,170,0,255),     // 3  dark green
    MAKE_COLOR(170,170,0,255),   // 4  dark yellow
    MAKE_COLOR(0,0,170,255),     // 5  dark blue
    MAKE_COLOR(170,0,170,255),   // 6  dark magenta
    MAKE_COLOR(0,170,170,255),   // 7  dark cyan
    MAKE_COLOR(170,170,170,255), // 8  light gray
    MAKE_COLOR(85,85,85,255),    // 9  dark gray
    MAKE_COLOR(255,85,85,255),   // 10 red
    MAKE_COLOR(85,255,85,255),   // 11 green
    MAKE_COLOR(255,255,85,255),  // 12 yellow
    MAKE_COLOR(85,85,255,255),   // 13 blue
    MAKE_COLOR(255,85,255,255),  // 14 magenta
    MAKE_COLOR(85,255,255,255),  // 15 cyan
    MAKE_COLOR(255,255,255,255), // 16 white
  };
  for (int i = 0; i < (int)(sizeof(kBasePalette)/sizeof(kBasePalette[0])); i++)
    doc->ipal.entries[i + 1] = kBasePalette[i];
  // Fill remaining slots with a 6x6x6 color cube (indices 17-232).
  int idx = 17;
  for (int r = 0; r < 6 && idx < 233; r++)
    for (int g = 0; g < 6 && idx < 233; g++)
      for (int b = 0; b < 6 && idx < 233; b++, idx++)
        doc->ipal.entries[idx] = MAKE_COLOR(r*51, g*51, b*51, 255);
  // Fill 233-255 with a 24-step grayscale ramp.
  for (int i = 0; i < 24 && idx < 256; i++, idx++)
    doc->ipal.entries[idx] = MAKE_COLOR(i*11, i*11, i*11, 255);
  doc->ipal.count = 256;
#endif // IMAGEEDITOR_BW
#endif

  // Always initialize the animation timeline with one frame capturing the
  // current canvas pixels.  Single-canvas workflows simply use frame 0.
  doc->anim = anim_timeline_new(buf_w, buf_h);
  if (!doc->anim || !doc_anim_commit(doc)) {
    anim_timeline_free(doc->anim); doc_free_layers(doc); free(doc->layer.composite_buf); free(doc);
    return NULL;
  }

  if (filename) {
    strncpy(doc->filename, filename, sizeof(doc->filename) - 1);
    doc->filename[sizeof(doc->filename) - 1] = '\0';
  }

  int max_view_w = 1;
  int max_view_h = 1;
  imageeditor_max_canvas_viewport_size(&max_view_w, &max_view_h);
  int viewport_w = MIN(w, max_view_w), viewport_h = MIN(h, max_view_h);
  int win_w = 1;
  int win_h = 1;
  imageeditor_document_frame_for_viewport(viewport_w, viewport_h, &win_w, &win_h);
  irect16_t ws = imageeditor_document_workspace_rect();
  set_default_window_position(ws.x, ws.y);

  flags_t flags = IMAGEEDITOR_DOCUMENT_STATUSBAR ? WINDOW_STATUSBAR : 0;
#ifndef AX_PLATFORM_IOS
  flags |= WINDOW_HSCROLL | WINDOW_VSCROLL;
#endif
  window_t *dwin = create_window(
      filename ? filename : "Untitled",
      flags,
      MAKERECT(CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h),
      NULL, doc_win_proc, g_app->hinstance, NULL);
  dwin->userdata = doc;
  doc->win = dwin;

  // Canvas child fills the document window's client area.
  irect16_t cr = get_client_rect(dwin);
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL,
      MAKERECT(0, 0, cr.w, cr.h),
      dwin, win_canvas_proc, 0, doc);
  cwin->flags &= ~WINDOW_NOTABSTOP;
  doc->canvas_win = cwin;

  dwin->maximizable = true;
  maximize_window(dwin);
  cr = get_client_rect(dwin);
  resize_window(cwin, cr.w, cr.h);
  int log_w = doc->canvas_w / MAX(1, g_bw_retina_scale);
  int log_h = doc->canvas_h / MAX(1, g_bw_retina_scale);
  if (!filename && log_w == cr.w && log_h == cr.h) {
    frect_t bounds = window_view_bounds(cwin);
    window_view_pan(cwin, (ipoint16_t){(int16_t)-lroundf(bounds.x), (int16_t)-lroundf(bounds.y)});
  } else {
    window_view_center(cwin);
  }
  show_window(dwin, true);

  doc->next   = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;
  imageeditor_sync_tool_palette();
#ifdef AX_PLATFORM_IOS
  send_message(dwin, evDisplayChange, 0, NULL);
#endif
  if (!g_app->main_toolbar_win)
    create_main_toolbar_window();
  imageeditor_sync_main_toolbar();

  doc_update_title(doc);
#if IMAGEEDITOR_DOCUMENT_STATUSBAR
  send_message(dwin, evStatusBar, 0,
               (void *)(filename ? filename : "New image"));
#endif

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
    R_DeleteTexture(doc->sel.floating.tex);
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
  imageeditor_sync_main_toolbar();
}
