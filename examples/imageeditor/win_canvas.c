// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"

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
      return true;
    }

    case kWindowMessageSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
      return false;

    case kWindowMessagePaint: {
      if (!state || !doc) return true;
      canvas_upload(doc);
      draw_rect(doc->canvas_tex,
                win->frame.x, win->frame.y,
                CANVAS_W * state->scale, CANVAS_H * state->scale);
      if (doc->sel_active) {
        int x0 = MIN(doc->sel_x0, doc->sel_x1) * state->scale;
        int y0 = MIN(doc->sel_y0, doc->sel_y1) * state->scale;
        int x1 = (MAX(doc->sel_x0, doc->sel_x1) + 1) * state->scale;
        int y1 = (MAX(doc->sel_y0, doc->sel_y1) + 1) * state->scale;
        int sw = x1 - x0;
        int sh = y1 - y0;
        int ox = win->frame.x + x0;
        int oy = win->frame.y + y0;
        fill_rect(0xFFFFFFFF, ox,          oy,          sw, 1);
        fill_rect(0xFFFFFFFF, ox,          oy + sh - 1, sw, 1);
        fill_rect(0xFFFFFFFF, ox,          oy,          1,  sh);
        fill_rect(0xFFFFFFFF, ox + sw - 1, oy,          1,  sh);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!state || !doc || !g_app) return true;
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
      int cx = lx / state->scale;
      int cy = ly / state->scale;
      doc->drawing = true;
      doc->last_x  = cx;
      doc->last_y  = cy;
      doc->sel_active = false;

      doc_push_undo(doc);

      switch (g_app->current_tool) {
        case TOOL_PENCIL:
          canvas_draw_circle(doc, cx, cy, 0, g_app->fg_color);
          break;
        case TOOL_BRUSH:
          canvas_draw_circle(doc, cx, cy, 2, g_app->fg_color);
          break;
        case TOOL_ERASER:
          canvas_draw_circle(doc, cx, cy, 3, g_app->bg_color);
          break;
        case TOOL_FILL:
          canvas_flood_fill(doc, cx, cy, g_app->fg_color);
          break;
        case TOOL_SELECT:
          doc->sel_x0 = doc->sel_x1 = cx;
          doc->sel_y0 = doc->sel_y1 = cy;
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
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
      int cx = lx / state->scale;
      int cy = ly / state->scale;
      if (cx == doc->last_x && cy == doc->last_y) return true;

      switch (g_app->current_tool) {
        case TOOL_PENCIL:
          canvas_draw_line(doc, doc->last_x, doc->last_y, cx, cy, 0, g_app->fg_color);
          break;
        case TOOL_BRUSH:
          canvas_draw_line(doc, doc->last_x, doc->last_y, cx, cy, 2, g_app->fg_color);
          break;
        case TOOL_ERASER:
          canvas_draw_line(doc, doc->last_x, doc->last_y, cx, cy, 3, g_app->bg_color);
          break;
        case TOOL_FILL:
          break;
        case TOOL_SELECT:
          doc->sel_x1 = cx;
          doc->sel_y1 = cy;
          break;
        default:
          break;
      }

      doc->last_x = cx;
      doc->last_y = cy;
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
