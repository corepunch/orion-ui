// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"
#include <SDL2/SDL.h>   // for SDL_GetModState / KMOD_SHIFT

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

  canvas_doc_t *doc = state->doc;
  int canvas_w = doc->canvas_w * state->scale;
  int canvas_h = doc->canvas_h * state->scale;
  int win_w    = win->frame.w;
  int win_h    = win->frame.h;

  bool need_h = canvas_w > win_w;
  bool need_v = canvas_h > win_h;

  // Resolve scrollbar interdependence: adding one scrollbar shrinks the
  // viewport in the perpendicular axis, which may force the other bar to
  // appear even if the content originally fit that axis.
  if (need_h && !need_v) need_v = canvas_h > win_h - SCROLLBAR_SIZE;
  if (need_v && !need_h) need_h = canvas_w > win_w - SCROLLBAR_SIZE;

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
  canvas_doc_t *doc = state->doc;
  int canvas_w = doc->canvas_w * state->scale;
  int canvas_h = doc->canvas_h * state->scale;

  // Mirror the scrollbar-interdependence logic from canvas_sync_scrollbars so
  // that the maximum pan correctly accounts for whichever scrollbars will be
  // shown.
  bool need_h = canvas_w > win_w;
  bool need_v = canvas_h > win_h;
  if (need_h && !need_v) need_v = canvas_h > win_h - SCROLLBAR_SIZE;
  if (need_v && !need_h) need_h = canvas_w > win_w - SCROLLBAR_SIZE;

  int view_w = need_v ? win_w - SCROLLBAR_SIZE : win_w;
  int view_h = need_h ? win_h - SCROLLBAR_SIZE : win_h;

  int max_x = MAX(0, canvas_w - view_w);
  int max_y = MAX(0, canvas_h - view_h);
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

// Apply a new zoom level centered on the canvas pixel (cx, cy) currently
// displayed at screen-local position (mx, my) inside the canvas frame.
// new_scale must be a valid zoom level; the pan is re-derived so the
// pointed-at canvas pixel stays under the cursor after zooming.
static void apply_zoom_centered(window_t *win, canvas_win_state_t *state,
                                int new_scale, int cx, int cy, int mx, int my) {
  state->pan_x = cx * new_scale - mx;
  state->pan_y = cy * new_scale - my;
  canvas_win_set_zoom(win, new_scale);
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
      // Sync scrollbars: show them if canvas is already larger than the window
      canvas_sync_scrollbars(win, s);
      return true;
    }

    case kWindowMessageDestroy: {
      if (state && state->mag_tex) {
        glDeleteTextures(1, &state->mag_tex);
        state->mag_tex = 0;
      }
      return false;
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
                doc->canvas_w * state->scale, doc->canvas_h * state->scale);

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
      // Polygon in-progress: draw a sel_rect bounding the rubber-band edge
      // from the last committed vertex to the current mouse position.
      if (doc->poly_active && doc->poly_count > 0) {
        point_t v0 = doc->poly_pts[doc->poly_count - 1];
        point_t v1 = doc->last;
        int px0 = MIN(v0.x, v1.x) * state->scale - state->pan_x;
        int py0 = MIN(v0.y, v1.y) * state->scale - state->pan_y;
        int px1 = (MAX(v0.x, v1.x) + 1) * state->scale - state->pan_x;
        int py1 = (MAX(v0.y, v1.y) + 1) * state->scale - state->pan_y;
        draw_sel_rect(win->frame.x + px0, win->frame.y + py0, px1 - px0, py1 - py0);
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

      // Magnifier tool: draw a loupe overlay in the top-right corner of the canvas
      // showing a 16×16 canvas-pixel region centered on the cursor at 4× zoom.
      // Rendered as a single textured quad to avoid 256 fill_rect() calls.
      enum { MAG_PIXELS = 16, MAG_ZOOM = 4, MAG_SIZE = MAG_PIXELS * MAG_ZOOM, MAG_MARGIN = 4 };
      if (g_app && g_app->current_tool == ID_TOOL_MAGNIFIER &&
          state->hover_valid &&
          win->frame.w  >= MAG_SIZE + MAG_MARGIN * 2 + 4 &&
          win->frame.h  >= MAG_SIZE + MAG_MARGIN * 2 + 4) {
        int lox = win->frame.x + win->frame.w - MAG_SIZE - MAG_MARGIN - 2;
        int loy = win->frame.y + MAG_MARGIN;
        // Border
        fill_rect(0xFF808080, lox - 1, loy - 1, MAG_SIZE + 2, MAG_SIZE + 2);
        // Build a 16×16 RGBA pixel buffer from the canvas region around hover
        uint8_t mag_buf[MAG_PIXELS * MAG_PIXELS * 4];
        int hx = state->hover.x - MAG_PIXELS / 2;
        int hy = state->hover.y - MAG_PIXELS / 2;
        for (int row = 0; row < MAG_PIXELS; row++) {
          for (int col = 0; col < MAG_PIXELS; col++) {
            int sx = hx + col, sy = hy + row;
            uint32_t px = canvas_in_bounds(doc, sx, sy)
                        ? canvas_get_pixel(doc, sx, sy)
                        : MAKE_COLOR(0x22, 0x22, 0x22, 0xFF);
            uint8_t *dst = mag_buf + (row * MAG_PIXELS + col) * 4;
            dst[0] = COLOR_R(px); dst[1] = COLOR_G(px); dst[2] = COLOR_B(px); dst[3] = COLOR_A(px);
          }
        }
        // Upload pixel buffer to a cached GL texture and draw as a single quad
        if (!state->mag_tex) {
          glGenTextures(1, &state->mag_tex);
          glBindTexture(GL_TEXTURE_2D, state->mag_tex);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MAG_PIXELS, MAG_PIXELS, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, mag_buf);
        } else {
          glBindTexture(GL_TEXTURE_2D, state->mag_tex);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAG_PIXELS, MAG_PIXELS,
                          GL_RGBA, GL_UNSIGNED_BYTE, mag_buf);
        }
        draw_rect(state->mag_tex, lox, loy, MAG_SIZE, MAG_SIZE);
        // Crosshair at loupe center
        int lcx = lox + MAG_SIZE / 2;
        int lcy = loy + MAG_SIZE / 2;
        fill_rect(0xFF000000, lcx - 3, lcy, 3, 1);
        fill_rect(0xFF000000, lcx + 1, lcy, 3, 1);
        fill_rect(0xFF000000, lcx, lcy - 3, 1, 3);
        fill_rect(0xFF000000, lcx, lcy + 1, 1, 3);
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
      int canvas_w  = doc->canvas_w * state->scale;
      int canvas_h  = doc->canvas_h * state->scale;
      // Mirror the scrollbar-interdependence logic so max pan is correct when
      // only one scrollbar is visible (its presence shrinks the other axis).
      bool need_h = canvas_w > win->frame.w;
      bool need_v = canvas_h > win->frame.h;
      if (need_h && !need_v) need_v = canvas_h > win->frame.h - SCROLLBAR_SIZE;
      if (need_v && !need_h) need_h = canvas_w > win->frame.w - SCROLLBAR_SIZE;
      int view_w    = need_v ? win->frame.w - SCROLLBAR_SIZE : win->frame.w;
      int view_h    = need_h ? win->frame.h - SCROLLBAR_SIZE : win->frame.h;
      int max_pan_x = MAX(0, canvas_w - view_w);
      int max_pan_y = MAX(0, canvas_h - view_h);
      if (max_pan_x > 0 || max_pan_y > 0) {
        // LOWORD = -wheel.x * SCROLL_SENSITIVITY; HIWORD = wheel.y * SCROLL_SENSITIVITY
        int dx = -(int16_t)LOWORD(wparam);  // natural scroll: flip x axis
        int dy = -(int16_t)HIWORD(wparam);  // natural scroll: flip y axis
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

      // Hand tool: begin pan drag in screen space
      if (g_app->current_tool == ID_TOOL_HAND) {
        state->panning = true;
        state->pan_start.x = lx;
        state->pan_start.y = ly;
        return true;
      }

      // Zoom tool (left click): zoom in centered on cursor
      if (g_app->current_tool == ID_TOOL_ZOOM) {
        int mx = lx - win->frame.x;
        int my = ly - win->frame.y;
        int cx = (mx + state->pan_x) / state->scale;
        int cy = (my + state->pan_y) / state->scale;
        int new_scale = state->scale;
        for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
          if (kZoomLevels[i] > state->scale) { new_scale = kZoomLevels[i]; break; }
        }
        if (new_scale != state->scale)
          apply_zoom_centered(win, state, new_scale, cx, cy, mx, my);
        return true;
      }

      // Eyedropper (left click): pick foreground color from canvas pixel
      if (g_app->current_tool == ID_TOOL_EYEDROPPER) {
        int px = (lx - win->frame.x + state->pan_x) / state->scale;
        int py = (ly - win->frame.y + state->pan_y) / state->scale;
        if (canvas_in_bounds(doc, px, py)) {
          g_app->fg_color = canvas_get_pixel(doc, px, py);
          if (g_app->tool_win)  invalidate_window(g_app->tool_win);
          if (g_app->color_win) invalidate_window(g_app->color_win);
        }
        return true;
      }

      // Magnifier tool: the loupe is a passive overlay; clicks have no effect
      if (g_app->current_tool == ID_TOOL_MAGNIFIER) return true;

      int px = (lx - win->frame.x + state->pan_x) / state->scale;
      int py = (ly - win->frame.y + state->pan_y) / state->scale;
      int tool = g_app->current_tool;

      // Text tool: record position and show text options dialog
      if (tool == ID_TOOL_TEXT) {
        if (!canvas_in_bounds(doc, px, py)) return true;
        text_options_t opts;
        memset(&opts, 0, sizeof(opts));
        opts.font_size = g_app->text_font_size;
        opts.color     = g_app->fg_color;
        opts.antialias = g_app->text_antialias;
        if (show_text_dialog(win, &opts) && opts.text[0]) {
          // Persist settings for next use
          g_app->text_font_size = opts.font_size;
          g_app->text_antialias = opts.antialias;
          doc_push_undo(doc);
          if (canvas_draw_text_stb(doc, px, py, &opts)) {
            doc->modified = true;
            doc_update_title(doc);
            invalidate_window(win);
          } else {
            doc_discard_undo(doc);
          }
        }
        return true;
      }

      // Polygon: accumulate vertices on each click; commit on right-click
      if (tool == ID_TOOL_POLYGON) {
        if (!doc->poly_active) {
          doc_push_undo(doc);
          canvas_shape_begin(doc, px, py);  // snapshot for cancel/undo
          doc->poly_active = true;
          doc->poly_count  = 0;
        }
        if (doc->poly_count < (int)(sizeof(doc->poly_pts)/sizeof(doc->poly_pts[0]))) {
          doc->poly_pts[doc->poly_count++] = (point_t){px, py};
        }
        doc->last.x = px;
        doc->last.y = py;
        invalidate_window(win);
        return true;
      }

      doc->drawing   = true;
      doc->last.x    = px;
      doc->last.y    = py;

      if (canvas_is_shape_tool(tool)) {
        // Shape tools: take snapshot then preview – undo is pushed on mouse-up
        canvas_shape_begin(doc, px, py);
        canvas_shape_preview(doc, px, py, px, py, tool,
                             g_app->shape_filled, g_app->fg_color, g_app->bg_color, false);
        invalidate_window(win);
        return true;
      }

      doc_push_undo(doc);

      switch (tool) {
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
        case ID_TOOL_SPRAY:
          canvas_spray(doc, px, py, 8, g_app->fg_color);
          break;
        case ID_TOOL_SELECT:
          // If clicking inside the existing selection → move mode
          if (doc->sel_active && canvas_in_selection(doc, px, py)) {
            canvas_begin_move(doc, g_app->bg_color);
            float_tex_upload(doc);
            doc->move_origin.x = px;
            doc->move_origin.y = py;
          } else {
            // Start a new selection; commit any in-progress move first.
            if (doc->sel_moving) canvas_commit_move(doc);
            doc->sel_active = false;
            doc->sel_start.x = doc->sel_end.x = px;
            doc->sel_start.y = doc->sel_end.y = py;
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

    case kWindowMessageRightButtonDown: {
      // Right-click while polygon is active: commit the polygon
      if (!doc || !g_app) return true;

      // Eyedropper (right click): pick background color from canvas pixel
      if (state && g_app->current_tool == ID_TOOL_EYEDROPPER) {
        window_t *root = get_root_window(win);
        int lx = (int16_t)LOWORD(wparam) - root->frame.x;
        int ly = (int16_t)HIWORD(wparam) - root->frame.y;
        int px = (lx - win->frame.x + state->pan_x) / state->scale;
        int py = (ly - win->frame.y + state->pan_y) / state->scale;
        if (canvas_in_bounds(doc, px, py)) {
          g_app->bg_color = canvas_get_pixel(doc, px, py);
          if (g_app->tool_win)  invalidate_window(g_app->tool_win);
          if (g_app->color_win) invalidate_window(g_app->color_win);
        }
        return true;
      }

      // Zoom tool (right click): zoom out centered on cursor
      if (state && g_app->current_tool == ID_TOOL_ZOOM) {
        window_t *root = get_root_window(win);
        int lx = (int16_t)LOWORD(wparam) - root->frame.x;
        int ly = (int16_t)HIWORD(wparam) - root->frame.y;
        int mx = lx - win->frame.x;
        int my = ly - win->frame.y;
        int cx = (mx + state->pan_x) / state->scale;
        int cy = (my + state->pan_y) / state->scale;
        int new_scale = state->scale;
        for (int i = NUM_ZOOM_LEVELS - 1; i >= 0; i--) {
          if (kZoomLevels[i] < state->scale) { new_scale = kZoomLevels[i]; break; }
        }
        if (new_scale != state->scale)
          apply_zoom_centered(win, state, new_scale, cx, cy, mx, my);
        return true;
      } else if (g_app->current_tool == ID_TOOL_POLYGON && doc->poly_active && doc->poly_count >= 2) {
        if (g_app->shape_filled)
          canvas_draw_polygon_filled(doc, doc->poly_pts, doc->poly_count, g_app->fg_color, g_app->bg_color);
        else
          canvas_draw_polygon_outline(doc, doc->poly_pts, doc->poly_count, g_app->fg_color);
        // Fix up undo: undo_states[top] was pushed with pre-draw pixels from shape_begin.
        // After drawing, swap undo entry (pre-draw) with shape_snapshot (drawn) to align them.
        // Actually the undo was already pushed correctly on first click via doc_push_undo
        // in the polygon start handler; shape_snapshot holds the pre-draw state.
        // We need undo_states[top] = pre-draw, but it currently = pre-draw (correct!).
        // Nothing extra needed — doc_push_undo was called at polygon start.
        doc->poly_active = false;
        doc->poly_count  = 0;
        doc->modified = true;
        doc_update_title(doc);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageMouseMove: {
      if (!state || !g_app) return true;

      // Hand tool: update pan while dragging
      if (state->panning) {
        window_t *root = get_root_window(win);
        int lx = (int16_t)LOWORD(wparam) - root->frame.x;
        int ly = (int16_t)HIWORD(wparam) - root->frame.y;
        state->pan_x -= lx - state->pan_start.x;
        state->pan_y -= ly - state->pan_start.y;
        state->pan_start.x = lx;
        state->pan_start.y = ly;
        clamp_pan(state, win->frame.w, win->frame.h);
        canvas_sync_scrollbars(win, state);
        invalidate_window(win);
        return true;
      }

      if (!doc) return true;

      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y;
      int px = (lx - win->frame.x + state->pan_x) / state->scale;
      int py = (ly - win->frame.y + state->pan_y) / state->scale;

      // Always track the hover position (used by the magnifier overlay)
      state->hover.x    = px;
      state->hover.y    = py;
      state->hover_valid = canvas_in_bounds(doc, px, py);

      int tool = g_app->current_tool;
      bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;

      // Magnifier: repaint to update the loupe overlay; nothing else to do
      if (tool == ID_TOOL_MAGNIFIER) {
        invalidate_window(win);
        return true;
      }

      // Update polygon rubber-band preview (stores last mouse position in doc->last)
      if (tool == ID_TOOL_POLYGON && doc->poly_active) {
        doc->last.x = px;
        doc->last.y = py;
        invalidate_window(win);
        return true;
      }

      if (!doc->drawing) return true;
      if (px == doc->last.x && py == doc->last.y) return true;

      if (canvas_is_shape_tool(tool)) {
        canvas_shape_preview(doc,
                             doc->shape_start.x, doc->shape_start.y,
                             px, py, tool,
                             g_app->shape_filled, g_app->fg_color, g_app->bg_color, shift);
        doc->last.x = px;
        doc->last.y = py;
        invalidate_window(win);
        return true;
      }

      switch (tool) {
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
        case ID_TOOL_SPRAY:
          canvas_spray(doc, px, py, 8, g_app->fg_color);
          break;
        case ID_TOOL_SELECT:
          if (doc->sel_moving) {
            int dx = px - doc->move_origin.x;
            int dy = py - doc->move_origin.y;
            doc->float_pos.x += dx;
            doc->float_pos.y += dy;
            doc->move_origin.x = px;
            doc->move_origin.y = py;
          } else {
            doc->sel_end.x = px;
            doc->sel_end.y = py;
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

    case kWindowMessageLeftButtonUp: {
      if (state) state->panning = false;
      if (!doc || !g_app) return true;
      int tool = g_app->current_tool;

      if (canvas_is_shape_tool(tool) && doc->drawing) {
        // Commit the final shape.  doc->pixels already has the drawn result.
        // We need the undo stack to hold the PRE-draw state (shape_snapshot).
        // doc_push_undo() saves the CURRENT pixels; after that we swap the
        // newly pushed entry (= drawn pixels) with shape_snapshot (= pre-draw)
        // so that undo correctly restores the pre-draw state.
        doc_push_undo(doc);
        if (doc->shape_snapshot && doc->undo_count > 0) {
          uint8_t *tmp = doc->undo_states[doc->undo_count - 1];
          doc->undo_states[doc->undo_count - 1] = doc->shape_snapshot;
          doc->shape_snapshot = tmp;  // reuse buffer next time
        }
        doc->drawing  = false;
        doc->modified = true;
        doc_update_title(doc);
        invalidate_window(win);
      } else {
        if (doc->sel_moving) {
          canvas_commit_move(doc);
          doc_update_title(doc);
        }
        doc->drawing = false;
      }
      return true;
    }

    case kWindowMessageKeyDown: {
      if (!doc || !g_app) return false;
      int tool = g_app->current_tool;
      // Escape cancels an in-progress polygon or shape drag
      if (wparam == SDL_SCANCODE_ESCAPE) {
        if (tool == ID_TOOL_POLYGON && doc->poly_active) {
          if (doc->shape_snapshot) {
            memcpy(doc->pixels, doc->shape_snapshot, (size_t)doc->canvas_w * doc->canvas_h * 4);
            doc->canvas_dirty = true;
          }
          doc_discard_undo(doc);  // drop the no-op undo entry pushed at polygon start
          doc->poly_active = false;
          doc->poly_count  = 0;
          invalidate_window(win);
          return true;
        }
        if (canvas_is_shape_tool(tool) && doc->drawing && doc->shape_snapshot) {
          memcpy(doc->pixels, doc->shape_snapshot, (size_t)doc->canvas_w * doc->canvas_h * 4);
          doc->canvas_dirty = true;
          doc->drawing = false;
          invalidate_window(win);
          return true;
        }
      }
      return false;
    }

    case kWindowMessageResize: {
      if (state) {
        clamp_pan(state, win->frame.w, win->frame.h);
        canvas_sync_scrollbars(win, state);
      }
      return false;
    }

    default:
      return false;
  }
}
