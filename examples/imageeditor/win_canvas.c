// Canvas child window – renders the pixel canvas and dispatches paint events to tools

result_t win_canvas_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  canvas_doc_t *doc = (canvas_doc_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      doc = (canvas_doc_t *)lparam;
      win->userdata = doc;
      doc->canvas_win = win;
      return true;

    case kWindowMessageSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
      return false;

    case kWindowMessagePaint: {
      if (!doc) return true;
      canvas_upload(doc);
      draw_rect(doc->canvas_tex,
                win->frame.x, win->frame.y,
                CANVAS_W * CANVAS_SCALE, CANVAS_H * CANVAS_SCALE);
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!doc || !g_app) return true;
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
      int cx = lx / CANVAS_SCALE;
      int cy = ly / CANVAS_SCALE;
      doc->drawing = true;
      doc->last_x  = cx;
      doc->last_y  = cy;

      const tool_t *t = (g_app->current_tool >= 0 && g_app->current_tool < NUM_TOOLS)
                        ? tools[g_app->current_tool] : NULL;
      if (t && t->on_down)
        t->on_down(doc, cx, cy, g_app->fg_color, g_app->bg_color);

      invalidate_window(win);
      doc_update_title(doc);
      return true;
    }

    case kWindowMessageMouseMove: {
      if (!doc || !doc->drawing || !g_app) return true;
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
      int cx = lx / CANVAS_SCALE;
      int cy = ly / CANVAS_SCALE;
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
