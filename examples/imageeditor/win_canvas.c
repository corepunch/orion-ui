// Canvas child window – renders the pixel canvas and dispatches paint events to tools

#include "imageeditor.h"
#include <SDL2/SDL.h>   // for SDL_GetModState / KMOD_SHIFT

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
        int x0 = MIN(doc->sel_start.x, doc->sel_end.x) * state->scale;
        int y0 = MIN(doc->sel_start.y, doc->sel_end.y) * state->scale;
        int x1 = (MAX(doc->sel_start.x, doc->sel_end.x) + 1) * state->scale;
        int y1 = (MAX(doc->sel_start.y, doc->sel_end.y) + 1) * state->scale;
        draw_sel_rect(win->frame.x + x0, win->frame.y + y0, x1 - x0, y1 - y0);
      }
      // Polygon in-progress: draw a sel_rect preview of the rubber-band edge
      if (doc->poly_active && doc->poly_count > 0) {
        point_t last = doc->poly_pts[doc->poly_count - 1];
        draw_sel_rect(win->frame.x + last.x * state->scale,
                      win->frame.y + last.y * state->scale, 2, 2);
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

      int tool = g_app->current_tool;

      // Polygon: accumulate vertices on each click; commit on right-click
      if (tool == ID_TOOL_POLYGON) {
        if (!doc->poly_active) {
          doc_push_undo(doc);
          canvas_shape_begin(doc, cx, cy);  // snapshot for cancel/undo
          doc->poly_active = true;
          doc->poly_count  = 0;
        }
        if (doc->poly_count < (int)(sizeof(doc->poly_pts)/sizeof(doc->poly_pts[0]))) {
          doc->poly_pts[doc->poly_count++] = (point_t){cx, cy};
        }
        doc->last.x = cx;
        doc->last.y = cy;
        invalidate_window(win);
        return true;
      }

      doc->drawing   = true;
      doc->last.x    = cx;
      doc->last.y    = cy;

      if (canvas_is_shape_tool(tool)) {
        // Shape tools: take snapshot then preview – undo is pushed on mouse-up
        canvas_shape_begin(doc, cx, cy);
        canvas_shape_preview(doc, cx, cy, cx, cy, tool,
                             g_app->shape_filled, g_app->fg_color, g_app->bg_color, false);
        invalidate_window(win);
        return true;
      }

      doc_push_undo(doc);

      switch (tool) {
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
      if (!state || !doc || !g_app) return true;
      window_t *root = get_root_window(win);
      int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
      int cx = lx / state->scale;
      int cy = ly / state->scale;

      int tool = g_app->current_tool;
      bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;

      // Update polygon rubber-band preview (stores last mouse position in doc->last)
      if (tool == ID_TOOL_POLYGON && doc->poly_active) {
        doc->last.x = cx;
        doc->last.y = cy;
        invalidate_window(win);
        return true;
      }

      if (!doc->drawing) return true;
      if (cx == doc->last.x && cy == doc->last.y) return true;

      if (canvas_is_shape_tool(tool)) {
        canvas_shape_preview(doc,
                             doc->shape_start.x, doc->shape_start.y,
                             cx, cy, tool,
                             g_app->shape_filled, g_app->fg_color, g_app->bg_color, shift);
        doc->last.x = cx;
        doc->last.y = cy;
        invalidate_window(win);
        return true;
      }

      switch (tool) {
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

    case kWindowMessageLeftButtonUp: {
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
        doc->drawing = false;
      }
      return true;
    }

    case kWindowMessageRightButtonDown: {
      // Right-click while polygon is active: commit the polygon
      if (!doc || !g_app) return true;
      int tool = g_app->current_tool;
      if (tool == ID_TOOL_POLYGON && doc->poly_active && doc->poly_count >= 2) {
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

    case kWindowMessageKeyDown: {
      if (!doc || !g_app) return false;
      int tool = g_app->current_tool;
      // Escape cancels an in-progress polygon or shape drag
      if (wparam == SDL_SCANCODE_ESCAPE) {
        if (tool == ID_TOOL_POLYGON && doc->poly_active) {
          if (doc->shape_snapshot) {
            memcpy(doc->pixels, doc->shape_snapshot, CANVAS_H * CANVAS_W * 4);
            doc->canvas_dirty = true;
          }
          doc->poly_active = false;
          doc->poly_count  = 0;
          invalidate_window(win);
          return true;
        }
        if (canvas_is_shape_tool(tool) && doc->drawing && doc->shape_snapshot) {
          memcpy(doc->pixels, doc->shape_snapshot, CANVAS_H * CANVAS_W * 4);
          doc->canvas_dirty = true;
          doc->drawing = false;
          invalidate_window(win);
          return true;
        }
      }
      return false;
    }

    default:
      return false;
  }
}
