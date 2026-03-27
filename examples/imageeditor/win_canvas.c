// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"

// Valid zoom levels (MacPaint/Photoshop 1.0 style)
static const int kZoomLevels[] = {1, 2, 4, 6, 8};
static const int kNumZoomLevels = 5;

// Clamp pan to the valid range for the current zoom level and window size
static void clamp_pan(canvas_win_state_t *state, int win_w, int win_h) {
  int max_x = MAX(0, CANVAS_W * state->scale - win_w);
  int max_y = MAX(0, CANVAS_H * state->scale - win_h);
  if (state->pan_x < 0) state->pan_x = 0;
  if (state->pan_y < 0) state->pan_y = 0;
  if (state->pan_x > max_x) state->pan_x = max_x;
  if (state->pan_y > max_y) state->pan_y = max_y;
}

// Set zoom level on a canvas window (called by menu/accelerator handler)
void canvas_win_set_zoom(window_t *win, int new_scale) {
  canvas_win_state_t *state = (canvas_win_state_t *)win->userdata;
  if (!state) return;
  state->scale = new_scale;
  clamp_pan(state, win->frame.w, win->frame.h);
  invalidate_window(win);
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

      if (doc->sel_active) {
        int x0 = MIN(doc->sel_start.x, doc->sel_end.x) * state->scale - state->pan_x;
        int y0 = MIN(doc->sel_start.y, doc->sel_end.y) * state->scale - state->pan_y;
        int x1 = (MAX(doc->sel_start.x, doc->sel_end.x) + 1) * state->scale - state->pan_x;
        int y1 = (MAX(doc->sel_start.y, doc->sel_end.y) + 1) * state->scale - state->pan_y;
        draw_sel_rect(win->frame.x + x0, win->frame.y + y0, x1 - x0, y1 - y0);
      }

      // Draw scrollbar indicators when zoomed content exceeds window boundaries
      {
        int canvas_w = CANVAS_W * state->scale;
        int canvas_h = CANVAS_H * state->scale;
        int vw = win->frame.w;
        int vh = win->frame.h;
        enum { SB = 4 };  // scrollbar thickness in pixels

        if (canvas_w > vw) {
          // Horizontal scrollbar track
          int bar_w = vw - (canvas_h > vh ? SB : 0);
          int track_x = win->frame.x;
          int track_y = win->frame.y + vh - SB;
          fill_rect(COLOR_PANEL_DARK_BG, track_x, track_y, bar_w, SB);
          // Thumb
          int thumb_w = MAX(8, bar_w * vw / canvas_w);
          int max_off  = canvas_w - vw;
          int thumb_x  = (max_off > 0) ? state->pan_x * (bar_w - thumb_w) / max_off : 0;
          fill_rect(COLOR_LIGHT_EDGE, track_x + thumb_x, track_y, thumb_w, SB);
        }

        if (canvas_h > vh) {
          // Vertical scrollbar track
          int bar_h = vh - (canvas_w > vw ? SB : 0);
          int track_x = win->frame.x + vw - SB;
          int track_y = win->frame.y;
          fill_rect(COLOR_PANEL_DARK_BG, track_x, track_y, SB, bar_h);
          // Thumb
          int thumb_h = MAX(8, bar_h * vh / canvas_h);
          int max_off  = canvas_h - vh;
          int thumb_y  = (max_off > 0) ? state->pan_y * (bar_h - thumb_h) / max_off : 0;
          fill_rect(COLOR_LIGHT_EDGE, track_x, track_y + thumb_y, SB, thumb_h);
        }
      }

      return true;
    }

    case kWindowMessageWheel: {
      if (!state) return false;
      int canvas_w = CANVAS_W * state->scale;
      int canvas_h = CANVAS_H * state->scale;
      int max_pan_x = MAX(0, canvas_w - win->frame.w);
      int max_pan_y = MAX(0, canvas_h - win->frame.h);
      if (max_pan_x > 0 || max_pan_y > 0) {
        // LOWORD = -wheel.x * SCROLL_SENSITIVITY; HIWORD = wheel.y * SCROLL_SENSITIVITY
        int dx = -(int16_t)LOWORD(wparam);  // positive dx → scroll right → increase pan_x
        int dy =  (int16_t)HIWORD(wparam);  // positive dy (scroll up) → decrease pan_y
        state->pan_x = MIN(MAX(state->pan_x + dx, 0), max_pan_x);
        state->pan_y = MIN(MAX(state->pan_y - dy, 0), max_pan_y);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageLeftButtonDown: {
      if (!state || !doc || !g_app) return true;
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x + state->pan_x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y + state->pan_y;
      int cx = lx / state->scale;
      int cy = ly / state->scale;
      doc->drawing = true;
      doc->last.x  = cx;
      doc->last.y  = cy;

      doc_push_undo(doc);

      switch (g_app->current_tool) {
        case ID_TOOL_PENCIL:
          canvas_draw_circle(doc, cx, cy, 0, g_app->fg_color);
          break;
        case ID_TOOL_BRUSH:
          canvas_draw_circle(doc, cx, cy, 2, g_app->fg_color);
          break;
        case ID_TOOL_ERASER:
          canvas_draw_circle(doc, cx, cy, 3, g_app->bg_color);
          break;
        case ID_TOOL_FILL:
          canvas_flood_fill(doc, cx, cy, g_app->fg_color);
          break;
        case ID_TOOL_SELECT:
          doc->sel_active = false;
          doc->sel_start.x = doc->sel_end.x = cx;
          doc->sel_start.y = doc->sel_end.y = cy;
          doc->sel_active = true;
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
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x + state->pan_x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y + state->pan_y;
      int cx = lx / state->scale;
      int cy = ly / state->scale;
      if (cx == doc->last.x && cy == doc->last.y) return true;

      switch (g_app->current_tool) {
        case ID_TOOL_PENCIL:
          canvas_draw_line(doc, doc->last.x, doc->last.y, cx, cy, 0, g_app->fg_color);
          break;
        case ID_TOOL_BRUSH:
          canvas_draw_line(doc, doc->last.x, doc->last.y, cx, cy, 2, g_app->fg_color);
          break;
        case ID_TOOL_ERASER:
          canvas_draw_line(doc, doc->last.x, doc->last.y, cx, cy, 3, g_app->bg_color);
          break;
        case ID_TOOL_FILL:
          break;
        case ID_TOOL_SELECT:
          doc->sel_end.x = cx;
          doc->sel_end.y = cy;
          break;
        default:
          break;
      }

      doc->last.x = cx;
      doc->last.y = cy;
      invalidate_window(win);
      return true;
    }

    case kWindowMessageLeftButtonUp:
      if (doc) doc->drawing = false;
      return true;

    default:
      return false;
  }
}
