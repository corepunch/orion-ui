// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"

// Single source of truth for zoom levels and their View menu IDs.
// win_menubar.c also references these via the extern declarations in imageeditor.h.
const int kZoomLevels[NUM_ZOOM_LEVELS]  = {1, 2, 4, 6, 8};
const int kZoomMenuIDs[NUM_ZOOM_LEVELS] = {
  ID_VIEW_ZOOM_1X, ID_VIEW_ZOOM_2X, ID_VIEW_ZOOM_4X,
  ID_VIEW_ZOOM_6X, ID_VIEW_ZOOM_8X
};

// ---- scrollbar helpers -------------------------------------------------------

// Update scrollbar info and visibility to match the current zoom/pan state.
// Called after any change to scale, pan, or window size.
static void canvas_sync_scrollbars(window_t *win, canvas_win_state_t *state) {
  if (!state->hscroll || !state->vscroll) return;

  int canvas_w = CANVAS_W * state->scale;
  int canvas_h = CANVAS_H * state->scale;
  int win_w    = win->frame.w;
  int win_h    = win->frame.h;

  bool need_h = canvas_w > win_w;
  bool need_v = canvas_h > win_h;

  // Viewport sizes account for the other scrollbar if both are shown
  int view_w = need_v ? win_w - SCROLLBAR_SIZE : win_w;
  int view_h = need_h ? win_h - SCROLLBAR_SIZE : win_h;

  if (need_h) {
    state->hscroll->frame = (rect_t){0, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE};
    scrollbar_info_t hi = { 0, canvas_w, view_w, state->pan_x };
    send_message(state->hscroll, kScrollBarMessageSetInfo, 0, &hi);
    state->hscroll->visible = true;
  } else {
    state->pan_x = 0;
    state->hscroll->visible = false;
  }

  if (need_v) {
    state->vscroll->frame = (rect_t){win_w - SCROLLBAR_SIZE, 0, SCROLLBAR_SIZE, view_h};
    scrollbar_info_t vi = { 0, canvas_h, view_h, state->pan_y };
    send_message(state->vscroll, kScrollBarMessageSetInfo, 0, &vi);
    state->vscroll->visible = true;
  } else {
    state->pan_y = 0;
    state->vscroll->visible = false;
  }
}

// Clamp pan to the valid range for the current zoom level and window size
static void clamp_pan(canvas_win_state_t *state, int win_w, int win_h) {
  int max_x = MAX(0, CANVAS_W * state->scale - win_w);
  int max_y = MAX(0, CANVAS_H * state->scale - win_h);
  if (state->pan_x < 0) state->pan_x = 0;
  if (state->pan_y < 0) state->pan_y = 0;
  if (state->pan_x > max_x) state->pan_x = max_x;
  if (state->pan_y > max_y) state->pan_y = max_y;
}

// Forward a mouse event from the canvas to a scrollbar child.
// The forwarded wparam is adjusted so that win_scrollbar's sb_axis() formula
// (LOWORD/HIWORD - root.frame.x/y) yields the correct scrollbar-local coord.
// Since canvas.frame.x/y == 0, LOWORD/HIWORD in canvas wparam == logical_x/y.
static bool canvas_forward_to_scrollbar(window_t *sb, uint32_t msg, uint32_t wparam) {
  if (!sb || !sb->visible) return false;
  int fwd_lo = (int16_t)LOWORD(wparam) - sb->frame.x;
  int fwd_hi = (int16_t)HIWORD(wparam) - sb->frame.y;
  return (bool)send_message(sb, msg, MAKEDWORD(fwd_lo, fwd_hi), NULL);
}

// Test whether canvas-local (cx, cy) falls inside a scrollbar child.
static bool canvas_hit_scrollbar(window_t *sb, int cx, int cy) {
  if (!sb || !sb->visible) return false;
  return cx >= sb->frame.x && cx < sb->frame.x + sb->frame.w
      && cy >= sb->frame.y && cy < sb->frame.y + sb->frame.h;
}

// Set zoom level on a canvas window (called by menu/accelerator handler).
// new_scale is snapped to the nearest supported zoom level so callers can
// never trigger a divide-by-zero or produce unexpected canvas sizes.
void canvas_win_set_zoom(window_t *win, int new_scale) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  if (!state) return;

  // Snap new_scale to the closest supported zoom level
  int clamped = kZoomLevels[0];
  if (new_scale <= kZoomLevels[0]) {
    clamped = kZoomLevels[0];
  } else if (new_scale >= kZoomLevels[NUM_ZOOM_LEVELS - 1]) {
    clamped = kZoomLevels[NUM_ZOOM_LEVELS - 1];
  } else {
    for (int i = 1; i < NUM_ZOOM_LEVELS; i++) {
      if (new_scale <= kZoomLevels[i]) {
        int dist_prev = new_scale - kZoomLevels[i - 1];
        int dist_curr = kZoomLevels[i] - new_scale;
        clamped = (dist_prev <= dist_curr) ? kZoomLevels[i - 1] : kZoomLevels[i];
        break;
      }
    }
  }

  state->scale = clamped;
  clamp_pan(state, win->frame.w, win->frame.h);
  canvas_sync_scrollbars(win, state);
  invalidate_window(win);
}

// Release the floating-selection GL texture if one exists.
static void float_tex_free(canvas_doc_t *doc) {
  if (doc->float_tex) {
    glDeleteTextures(1, &doc->float_tex);
    doc->float_tex = 0;
  }
}

// Upload float_pixels into a (re)created float_tex.
static void float_tex_upload(canvas_doc_t *doc) {
  float_tex_free(doc);
  if (!doc->float_pixels || doc->float_w <= 0 || doc->float_h <= 0) return;
  glGenTextures(1, &doc->float_tex);
  glBindTexture(GL_TEXTURE_2D, doc->float_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
               doc->float_w, doc->float_h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, doc->float_pixels);
}

result_t win_canvas_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  canvas_doc_t *doc = state ? state->doc : NULL;
  switch (msg) {
    case kWindowMessageCreate: {
      canvas_win_state_t *s = allocate_window_data(win, sizeof(canvas_win_state_t));
      s->doc = (canvas_doc_t *)lparam;
      s->doc->canvas_win = win;
      s->scale = 1;
      s->pan_x = 0;
      s->pan_y = 0;
      // Create interactive scrollbar children.  Both start hidden; they are
      // shown and repositioned by canvas_sync_scrollbars() on demand.
      s->hscroll = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL,
          MAKERECT(0, win->frame.h - SCROLLBAR_SIZE,
                   win->frame.w - SCROLLBAR_SIZE, SCROLLBAR_SIZE),
          win, win_scrollbar, NULL);
      s->vscroll = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(win->frame.w - SCROLLBAR_SIZE, 0,
                   SCROLLBAR_SIZE, win->frame.h - SCROLLBAR_SIZE),
          win, win_scrollbar, NULL);
      s->hscroll->visible = false;
      s->vscroll->visible = false;
      return true;
    }

    case kWindowMessageSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
      return false;

    case kWindowMessagePaint: {
      if (!state || !doc) return true;
      canvas_upload(doc);

      // Draw canvas offset by pan so zoomed content scrolls correctly
      int cx = win->frame.x - state->pan_x;
      int cy = win->frame.y - state->pan_y;
      draw_rect(doc->canvas_tex,
                cx, cy,
                CANVAS_W * state->scale, CANVAS_H * state->scale);

      if (doc->sel_moving && doc->float_tex) {
        // Draw the floating selection at its current position
        int sx = win->frame.x + doc->float_pos.x * state->scale;
        int sy = win->frame.y + doc->float_pos.y * state->scale;
        int sw = doc->float_w * state->scale;
        int sh = doc->float_h * state->scale;
        draw_rect(doc->float_tex, sx, sy, sw, sh);
        draw_sel_rect(sx, sy, sw, sh);
      } else if (doc->sel_active) {
        int x0 = MIN(doc->sel_start.x, doc->sel_end.x) * state->scale - state->pan_x;
        int y0 = MIN(doc->sel_start.y, doc->sel_end.y) * state->scale - state->pan_y;
        int x1 = (MAX(doc->sel_start.x, doc->sel_end.x) + 1) * state->scale - state->pan_x;
        int y1 = (MAX(doc->sel_start.y, doc->sel_end.y) + 1) * state->scale - state->pan_y;
        draw_sel_rect(win->frame.x + x0, win->frame.y + y0, x1 - x0, y1 - y0);
      }

      // Paint scrollbar children on top of the canvas content
      if (state->hscroll && state->hscroll->visible)
        send_message(state->hscroll, kWindowMessagePaint, wparam, lparam);
      if (state->vscroll && state->vscroll->visible)
        send_message(state->vscroll, kWindowMessagePaint, wparam, lparam);

      // Fill the corner square when both bars are visible
      if (state->hscroll && state->hscroll->visible &&
          state->vscroll && state->vscroll->visible) {
        fill_rect(COLOR_PANEL_DARK_BG,
                  win->frame.x + win->frame.w - SCROLLBAR_SIZE,
                  win->frame.y + win->frame.h - SCROLLBAR_SIZE,
                  SCROLLBAR_SIZE, SCROLLBAR_SIZE);
      }

      return true;
    }

    case kWindowMessageCommand: {
      // Scrollbar child notifications: update pan and re-sync bars
      if (!state) return false;
      uint16_t code = HIWORD(wparam);
      uint16_t id   = LOWORD(wparam);
      if (code == kScrollBarNotificationChanged) {
        int new_pos = (int)(intptr_t)lparam;
        if (state->hscroll && id == state->hscroll->id)
          state->pan_x = new_pos;
        else if (state->vscroll && id == state->vscroll->id)
          state->pan_y = new_pos;
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageWheel: {
      if (!state) return false;
      int canvas_w  = CANVAS_W * state->scale;
      int canvas_h  = CANVAS_H * state->scale;
      int max_pan_x = MAX(0, canvas_w - win->frame.w);
      int max_pan_y = MAX(0, canvas_h - win->frame.h);
      if (max_pan_x > 0 || max_pan_y > 0) {
        // LOWORD = -wheel.x * SCROLL_SENSITIVITY; HIWORD = wheel.y * SCROLL_SENSITIVITY
        int dx =  (int16_t)LOWORD(wparam);  // positive → scroll right → increase pan_x
        int dy = -(int16_t)HIWORD(wparam);  // positive → scroll down  → increase pan_y
        state->pan_x = MIN(MAX(state->pan_x + dx, 0), max_pan_x);
        state->pan_y = MIN(MAX(state->pan_y + dy, 0), max_pan_y);
        canvas_sync_scrollbars(win, state);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageLeftButtonDown: {
      if (!state) return true;
      // canvas-local coords: logical_x - root.frame.x  (canvas.frame.x == 0)
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y;

      // Route clicks on scrollbar areas to the scrollbar control first
      if (canvas_hit_scrollbar(state->hscroll, lx, ly))
        return canvas_forward_to_scrollbar(state->hscroll, msg, wparam);
      if (canvas_hit_scrollbar(state->vscroll, lx, ly))
        return canvas_forward_to_scrollbar(state->vscroll, msg, wparam);

      if (!doc || !g_app) return true;
      int px = (lx - win->frame.x + state->pan_x) / state->scale;
      int py = (ly - win->frame.y + state->pan_y) / state->scale;
      doc->drawing = true;
      doc->last.x  = px;
      doc->last.y  = py;

      doc_push_undo(doc);

      switch (g_app->current_tool) {
        case ID_TOOL_PENCIL:
          canvas_draw_circle(doc, px, py, 0, g_app->fg_color);
          break;
        case ID_TOOL_BRUSH:
          canvas_draw_circle(doc, px, py, 2, g_app->fg_color);
          break;
        case ID_TOOL_ERASER:
          canvas_draw_circle(doc, px, py, 3, g_app->bg_color);
          break;
        case ID_TOOL_FILL:
          canvas_flood_fill(doc, px, py, g_app->fg_color);
          break;
        case ID_TOOL_SELECT:
          // If clicking inside the existing selection → move mode
          if (doc->sel_active && canvas_in_selection(doc, cx, cy)) {
            canvas_begin_move(doc, g_app->bg_color);
            float_tex_upload(doc);
            doc->move_origin.x = cx;
            doc->move_origin.y = cy;
          } else {
            // Start a new selection; commit any in-progress move first.
            if (doc->sel_moving) canvas_commit_move(doc);
            doc->sel_active = false;
            doc->sel_start.x = doc->sel_end.x = cx;
            doc->sel_start.y = doc->sel_end.y = cy;
            doc->sel_active = true;
          }
          break;
        default:
          break;
      }

      invalidate_window(win);
      doc_update_title(doc);
      return true;
    }

    case kWindowMessageMouseMove: {
      if (!state || !doc || !doc->drawing || !g_app) return true;
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y;
      int px = (lx - win->frame.x + state->pan_x) / state->scale;
      int py = (ly - win->frame.y + state->pan_y) / state->scale;
      if (px == doc->last.x && py == doc->last.y) return true;

      switch (g_app->current_tool) {
        case ID_TOOL_PENCIL:
          canvas_draw_line(doc, doc->last.x, doc->last.y, px, py, 0, g_app->fg_color);
          break;
        case ID_TOOL_BRUSH:
          canvas_draw_line(doc, doc->last.x, doc->last.y, px, py, 2, g_app->fg_color);
          break;
        case ID_TOOL_ERASER:
          canvas_draw_line(doc, doc->last.x, doc->last.y, px, py, 3, g_app->bg_color);
          break;
        case ID_TOOL_FILL:
          break;
        case ID_TOOL_SELECT:
          if (doc->sel_moving) {
            int dx = cx - doc->move_origin.x;
            int dy = cy - doc->move_origin.y;
            doc->float_pos.x += dx;
            doc->float_pos.y += dy;
            doc->move_origin.x = cx;
            doc->move_origin.y = cy;
          } else {
            doc->sel_end.x = cx;
            doc->sel_end.y = cy;
          }
          break;
        default:
          break;
      }

      doc->last.x = px;
      doc->last.y = py;
      invalidate_window(win);
      return true;
    }

    case kWindowMessageLeftButtonUp:
      if (doc) {
        if (doc->sel_moving) {
          canvas_commit_move(doc);
          doc_update_title(doc);
        }
        doc->drawing = false;
      }
      return true;

    default:
      return false;
  }
}
