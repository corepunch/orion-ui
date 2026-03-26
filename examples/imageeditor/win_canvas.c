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

      doc_push_undo(doc);

      const tool_t *t = (g_app->current_tool >= 0 && g_app->current_tool < NUM_TOOLS)
                        ? tools[g_app->current_tool] : NULL;
      if (t && t->on_down)
        t->on_down(doc, cx, cy, g_app->fg_color, g_app->bg_color);

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

      const tool_t *t = (g_app->current_tool >= 0 && g_app->current_tool < NUM_TOOLS)
                        ? tools[g_app->current_tool] : NULL;
      if (t && t->on_drag)
        t->on_drag(doc, doc->last_x, doc->last_y, cx, cy,
                   g_app->fg_color, g_app->bg_color);

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
