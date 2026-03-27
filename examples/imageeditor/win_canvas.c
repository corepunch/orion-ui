// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"

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
      if (doc->sel_moving && doc->float_tex) {
        // Draw the floating selection at its current position
        int sx = win->frame.x + doc->float_pos.x * state->scale;
        int sy = win->frame.y + doc->float_pos.y * state->scale;
        int sw = doc->float_w * state->scale;
        int sh = doc->float_h * state->scale;
        draw_rect(doc->float_tex, sx, sy, sw, sh);
        draw_sel_rect(sx, sy, sw, sh);
      } else if (doc->sel_active) {
        int x0 = MIN(doc->sel_start.x, doc->sel_end.x) * state->scale;
        int y0 = MIN(doc->sel_start.y, doc->sel_end.y) * state->scale;
        int x1 = (MAX(doc->sel_start.x, doc->sel_end.x) + 1) * state->scale;
        int y1 = (MAX(doc->sel_start.y, doc->sel_end.y) + 1) * state->scale;
        draw_sel_rect(win->frame.x + x0, win->frame.y + y0, x1 - x0, y1 - y0);
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
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
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

      doc->last.x = cx;
      doc->last.y = cy;
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
