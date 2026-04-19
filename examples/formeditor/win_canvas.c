// Form canvas window — renders the design surface, handles element
// selection/move/resize with drag handles, rubber-band placement of new
// controls, and built-in scrollbars when the form is larger than the viewport.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// ============================================================
// Constants
// ============================================================

// Size of each resize-handle square drawn around the selection.
#define HANDLE_SIZE  5
#define HANDLE_HALF  (HANDLE_SIZE / 2)

// Minimum element dimensions after a resize drag.
#define MIN_ELEM_W  10
#define MIN_ELEM_H   8

// ============================================================
// Handle indices
//   0=TL  1=TC  2=TR
//   3=ML        4=MR
//   5=BL  6=BC  7=BR
// ============================================================
#define HANDLE_TL    0
#define HANDLE_TC    1
#define HANDLE_TR    2
#define HANDLE_ML    3
#define HANDLE_MR    4
#define HANDLE_BL    5
#define HANDLE_BC    6
#define HANDLE_BR    7
#define HANDLE_COUNT 8

// ============================================================
// Coordinate helpers
// ============================================================

// Convert form-local to absolute screen X for a canvas window.
static inline int form_to_sx(window_t *win, canvas_state_t *s, int fx) {
  return win->frame.x + CANVAS_PADDING - s->pan_x + fx;
}
static inline int form_to_sy(window_t *win, canvas_state_t *s, int fy) {
  return win->frame.y + CANVAS_PADDING - s->pan_y + fy;
}

// Convert window-local mouse X to form-local.
static inline int local_to_form_x(canvas_state_t *s, int lx) {
  return lx - CANVAS_PADDING + s->pan_x;
}
static inline int local_to_form_y(canvas_state_t *s, int ly) {
  return ly - CANVAS_PADDING + s->pan_y;
}

// ============================================================
// Scrollbar helpers  (hscroll on doc_win, vscroll on canvas_win)
// ============================================================
static void canvas_sync_scrollbars(window_t *win, canvas_state_t *s) {
  form_doc_t *doc = s->doc;
  int content_w = doc->form_w + CANVAS_PADDING * 2;
  int content_h = doc->form_h + CANVAS_PADDING * 2;
  // vscroll strip occupies the rightmost SCROLLBAR_WIDTH pixels of the canvas.
  int view_w = win->frame.w - SCROLLBAR_WIDTH;
  int view_h = win->frame.h;

  scroll_info_t si;
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin  = 0;

  // Horizontal: owned by doc_win (merged with status bar).
  si.nMax  = content_w;
  si.nPage = view_w;
  si.nPos  = s->pan_x;
  set_scroll_info(doc->doc_win, SB_HORZ, &si, false);

  // Vertical: owned by this canvas window.
  si.nMax  = content_h;
  si.nPage = view_h;
  si.nPos  = s->pan_y;
  set_scroll_info(win, SB_VERT, &si, false);
}

static void canvas_clamp_pan(canvas_state_t *s, int win_w, int win_h) {
  form_doc_t *doc = s->doc;
  int max_x = MAX(0, doc->form_w + CANVAS_PADDING * 2 - (win_w - SCROLLBAR_WIDTH));
  int max_y = MAX(0, doc->form_h + CANVAS_PADDING * 2 - win_h);
  if (s->pan_x < 0) s->pan_x = 0;
  if (s->pan_y < 0) s->pan_y = 0;
  if (s->pan_x > max_x) s->pan_x = max_x;
  if (s->pan_y > max_y) s->pan_y = max_y;
}

// ============================================================
// Hit testing
// ============================================================

// Return index into doc->elements hit by (lx, ly) in window-local coords,
// or -1 if nothing was hit.  Tests in reverse paint order so topmost wins.
static int hit_test_elements(canvas_state_t *s, int lx, int ly) {
  form_doc_t *doc = s->doc;
  for (int i = doc->element_count - 1; i >= 0; i--) {
    form_element_t *el = &doc->elements[i];
    int ex = CANVAS_PADDING - s->pan_x + el->x;
    int ey = CANVAS_PADDING - s->pan_y + el->y;
    if (lx >= ex && lx < ex + el->w && ly >= ey && ly < ey + el->h)
      return i;
  }
  return -1;
}

// Compute the 8 handle positions (top-left corners) in window-local coords
// for the selected element, stored into out[HANDLE_COUNT].
static void get_handle_rects(canvas_state_t *s, form_element_t *el,
                              int out_x[HANDLE_COUNT], int out_y[HANDLE_COUNT]) {
  int ex = CANVAS_PADDING - s->pan_x + el->x;
  int ey = CANVAS_PADDING - s->pan_y + el->y;
  int ew = el->w;
  int eh = el->h;
  int cx = ex + ew / 2 - HANDLE_HALF;
  int cy = ey + eh / 2 - HANDLE_HALF;

  out_x[HANDLE_TL] = ex - HANDLE_HALF;        out_y[HANDLE_TL] = ey - HANDLE_HALF;
  out_x[HANDLE_TC] = cx;                       out_y[HANDLE_TC] = ey - HANDLE_HALF;
  out_x[HANDLE_TR] = ex + ew - HANDLE_HALF;   out_y[HANDLE_TR] = ey - HANDLE_HALF;
  out_x[HANDLE_ML] = ex - HANDLE_HALF;        out_y[HANDLE_ML] = cy;
  out_x[HANDLE_MR] = ex + ew - HANDLE_HALF;   out_y[HANDLE_MR] = cy;
  out_x[HANDLE_BL] = ex - HANDLE_HALF;        out_y[HANDLE_BL] = ey + eh - HANDLE_HALF;
  out_x[HANDLE_BC] = cx;                       out_y[HANDLE_BC] = ey + eh - HANDLE_HALF;
  out_x[HANDLE_BR] = ex + ew - HANDLE_HALF;   out_y[HANDLE_BR] = ey + eh - HANDLE_HALF;
}

// Return which resize handle (0-7) is under (lx, ly), or -1 if none.
static int hit_test_handles(canvas_state_t *s, int lx, int ly) {
  if (s->selected_idx < 0) return -1;
  form_element_t *el = &s->doc->elements[s->selected_idx];
  int hx[HANDLE_COUNT], hy[HANDLE_COUNT];
  get_handle_rects(s, el, hx, hy);
  for (int i = 0; i < HANDLE_COUNT; i++) {
    if (lx >= hx[i] && lx < hx[i] + HANDLE_SIZE &&
        ly >= hy[i] && ly < hy[i] + HANDLE_SIZE)
      return i;
  }
  return -1;
}

// ============================================================
// Drawing helpers
// ============================================================

// Clamp a rectangle to stay within the form surface bounds.
static void clamp_to_form(form_doc_t *doc, int *x, int *y, int *w, int *h) {
  if (*x < 0) { *w += *x; *x = 0; }
  if (*y < 0) { *h += *y; *y = 0; }
  if (*x + *w > doc->form_w) *w = doc->form_w - *x;
  if (*y + *h > doc->form_h) *h = doc->form_h - *y;
  if (*w < 1) *w = 1;
  if (*h < 1) *h = 1;
}

// Draw a sunken (inset) box at absolute screen coords.
static void draw_sunken_box(int sx, int sy, int sw, int sh) {
  fill_rect(0xFFFFFFFF, sx, sy, sw, sh);
  fill_rect(get_sys_color(kColorDarkEdge),  sx, sy, sw, 1);
  fill_rect(get_sys_color(kColorDarkEdge),  sx, sy, 1, sh);
  fill_rect(get_sys_color(kColorLightEdge), sx, sy + sh, sw, 1);
  fill_rect(get_sys_color(kColorLightEdge), sx + sw, sy, 1, sh);
}

// Draw a control element at its form-space position translated to screen.
static void draw_element(window_t *win, canvas_state_t *s, form_element_t *el) {
  int sx = form_to_sx(win, s, el->x);
  int sy = form_to_sy(win, s, el->y);
  int sw = el->w;
  int sh = el->h;
  uint32_t text_col = get_sys_color(kColorTextNormal);

  switch (el->type) {
    case CTRL_BUTTON: {
      rect_t r = {sx, sy, sw, sh};
      draw_button(&r, 1, 1, false);
      int tw = strwidth(el->text);
      int tx = sx + (sw - tw) / 2;
      int ty = sy + (sh - 8) / 2;
      if (tx < sx) tx = sx + 2;
      if (ty < sy) ty = sy + 1;
      draw_text_small(el->text, tx, ty, text_col);
      break;
    }
    case CTRL_CHECKBOX: {
      int bx = sx + 1;
      int by = sy + (sh - 8) / 2;
      if (by < sy) by = sy;
      fill_rect(0xFFFFFFFF, bx, by, 8, 8);
      fill_rect(get_sys_color(kColorDarkEdge), bx, by, 8, 1);
      fill_rect(get_sys_color(kColorDarkEdge), bx, by, 1, 8);
      fill_rect(get_sys_color(kColorLightEdge), bx, by+8, 8, 1);
      fill_rect(get_sys_color(kColorLightEdge), bx+8, by, 1, 8);
      draw_text_small(el->text, bx + 12, by, text_col);
      break;
    }
    case CTRL_LABEL:
      draw_text_small(el->text, sx + 1, sy + (sh - 8) / 2, text_col);
      break;
    case CTRL_TEXTEDIT:
      draw_sunken_box(sx, sy, sw, sh);
      draw_text_small(el->text, sx + 2, sy + (sh - 8) / 2, text_col);
      break;
    case CTRL_LIST:
      draw_sunken_box(sx, sy, sw, sh);
      for (int row = 0; row + 10 < sh; row += 10)
        fill_rect(get_sys_color(kColorWindowDarkBg), sx + 1, sy + row + 9, sw - 2, 1);
      break;
    case CTRL_COMBOBOX: {
      draw_sunken_box(sx, sy, sw, sh);
      int aw = 10;
      rect_t btn = {sx + sw - aw, sy, aw, sh};
      draw_button(&btn, 1, 1, false);
      draw_text_small(el->text, sx + 2, sy + (sh - 8) / 2, text_col);
      break;
    }
    default:
      fill_rect(get_sys_color(kColorWindowBg), sx, sy, sw, sh);
      break;
  }
}

// Draw the 8 resize handles around the selected element.
static void draw_handles(window_t *win, canvas_state_t *s) {
  if (s->selected_idx < 0) return;
  form_element_t *el = &s->doc->elements[s->selected_idx];
  int hx[HANDLE_COUNT], hy[HANDLE_COUNT];
  get_handle_rects(s, el, hx, hy);

  // Dotted selection border (4-pixel segments, screen coords)
  int bx = form_to_sx(win, s, el->x) - 1;
  int by = form_to_sy(win, s, el->y) - 1;
  int bw = el->w + 2;
  int bh = el->h + 2;
  draw_sel_rect(bx, by, bw, bh);

  // Solid handle squares
  uint32_t hcol = 0xFF000000;
  for (int i = 0; i < HANDLE_COUNT; i++)
    fill_rect(hcol, hx[i] + win->frame.x, hy[i] + win->frame.y, HANDLE_SIZE, HANDLE_SIZE);
}

// Draw a rubber-band rectangle (for placement drag) in form coords.
static void draw_rubber_band(window_t *win, canvas_state_t *s) {
  if (s->drag_mode != DRAG_RUBBERBND) return;
  int x0 = s->rb_x < 0 ? 0 : s->rb_x;
  int y0 = s->rb_y < 0 ? 0 : s->rb_y;
  int x1 = x0 + s->rb_w;
  int y1 = y0 + s->rb_h;
  if (x1 > s->doc->form_w) x1 = s->doc->form_w;
  if (y1 > s->doc->form_h) y1 = s->doc->form_h;
  if (x1 <= x0 || y1 <= y0) return;
  int sx = form_to_sx(win, s, x0);
  int sy = form_to_sy(win, s, y0);
  draw_sel_rect(sx, sy, x1 - x0, y1 - y0);
}

// ============================================================
// Tool -> control type mapping
// ============================================================
static int tool_to_ctrl_type(int tool) {
  switch (tool) {
    case ID_TOOL_BUTTON:   return CTRL_BUTTON;
    case ID_TOOL_CHECKBOX: return CTRL_CHECKBOX;
    case ID_TOOL_LABEL:    return CTRL_LABEL;
    case ID_TOOL_TEXTEDIT: return CTRL_TEXTEDIT;
    case ID_TOOL_LIST:     return CTRL_LIST;
    case ID_TOOL_COMBOBOX: return CTRL_COMBOBOX;
    default:               return -1;
  }
}

// Default dimensions for newly placed controls.
static void default_ctrl_size(int type, int *w, int *h) {
  switch (type) {
    case CTRL_BUTTON:   *w = 75;  *h = 23; break;
    case CTRL_CHECKBOX: *w = 97;  *h = 17; break;
    case CTRL_LABEL:    *w = 65;  *h = 13; break;
    case CTRL_TEXTEDIT: *w = 121; *h = 20; break;
    case CTRL_LIST:     *w = 121; *h = 60; break;
    case CTRL_COMBOBOX: *w = 121; *h = 20; break;
    default:            *w = 80;  *h = 20; break;
  }
}

// Control type display names for use in caption and name generation.
static const char *ctrl_type_name(int type) {
  switch (type) {
    case CTRL_BUTTON:   return "Button";
    case CTRL_CHECKBOX: return "CheckBox";
    case CTRL_LABEL:    return "Label";
    case CTRL_TEXTEDIT: return "TextBox";
    case CTRL_LIST:     return "ListBox";
    case CTRL_COMBOBOX: return "ComboBox";
    default:            return "Control";
  }
}

// ============================================================
// Add a new element to the document
// ============================================================
static void canvas_add_element(form_doc_t *doc, int type, int x, int y, int w, int h) {
  if (doc->element_count >= MAX_ELEMENTS) return;
  if (type < 0 || type >= CTRL_TYPE_COUNT) return;
  if (w < MIN_ELEM_W) w = MIN_ELEM_W;
  if (h < MIN_ELEM_H) h = MIN_ELEM_H;

  form_element_t *el = &doc->elements[doc->element_count];
  el->type  = type;
  el->id    = doc->next_id++;
  el->x     = x;
  el->y     = y;
  el->w     = w;
  el->h     = h;
  el->flags = 0;

  int n = ++doc->type_counters[type];
  // Caption (text shown inside the control)
  snprintf(el->text, sizeof(el->text), "%s%d", ctrl_type_name(type), n);
  // Identifier name (used in .h file)
  char pfx[8];
  switch (type) {
    case CTRL_BUTTON:   strncpy(pfx, "IDC_BTN",  sizeof(pfx) - 1); break;
    case CTRL_CHECKBOX: strncpy(pfx, "IDC_CHK",  sizeof(pfx) - 1); break;
    case CTRL_LABEL:    strncpy(pfx, "IDC_LBL",  sizeof(pfx) - 1); break;
    case CTRL_TEXTEDIT: strncpy(pfx, "IDC_EDT",  sizeof(pfx) - 1); break;
    case CTRL_LIST:     strncpy(pfx, "IDC_LST",  sizeof(pfx) - 1); break;
    case CTRL_COMBOBOX: strncpy(pfx, "IDC_CMB",  sizeof(pfx) - 1); break;
    default:            strncpy(pfx, "IDC_CTRL", sizeof(pfx) - 1); break;
  }
  pfx[sizeof(pfx)-1] = '\0';
  snprintf(el->name, sizeof(el->name), "%s%d", pfx, n);

  doc->element_count++;
  doc->modified = true;
  form_doc_update_title(doc);
}

// ============================================================
// Apply resize delta to the selected element
// ============================================================
static void canvas_apply_resize(canvas_state_t *s, int dx, int dy) {
  form_element_t *el = &s->doc->elements[s->selected_idx];
  int x = s->snap_x, y = s->snap_y, w = s->snap_w, h = s->snap_h;

  switch (s->drag_handle) {
    case HANDLE_TL: x += dx; y += dy; w -= dx; h -= dy; break;
    case HANDLE_TC:           y += dy;           h -= dy; break;
    case HANDLE_TR:           y += dy; w += dx;  h -= dy; break;
    case HANDLE_ML: x += dx;           w -= dx;           break;
    case HANDLE_MR:           w += dx;                    break;
    case HANDLE_BL: x += dx;           w -= dx; h += dy;  break;
    case HANDLE_BC:                              h += dy;  break;
    case HANDLE_BR:           w += dx;           h += dy;  break;
    default: break;
  }
  if (w < MIN_ELEM_W) {
    if (s->drag_handle == HANDLE_TL || s->drag_handle == HANDLE_ML ||
        s->drag_handle == HANDLE_BL) x = s->snap_x + s->snap_w - MIN_ELEM_W;
    w = MIN_ELEM_W;
  }
  if (h < MIN_ELEM_H) {
    if (s->drag_handle == HANDLE_TL || s->drag_handle == HANDLE_TC ||
        s->drag_handle == HANDLE_TR) y = s->snap_y + s->snap_h - MIN_ELEM_H;
    h = MIN_ELEM_H;
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  el->x = x; el->y = y; el->w = w; el->h = h;
}

// ============================================================
// Window procedure
// ============================================================
result_t win_canvas_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  canvas_state_t *s = (canvas_state_t *)win->userdata;
  form_doc_t *doc = s ? s->doc : NULL;

  switch (msg) {
    case evCreate: {
      canvas_state_t *st = allocate_window_data(win, sizeof(canvas_state_t));
      st->doc          = (form_doc_t *)lparam;
      st->selected_idx = -1;
      st->pan_x        = 0;
      st->pan_y        = 0;
      st->drag_mode    = DRAG_NONE;
      st->drag_handle  = -1;
      canvas_sync_scrollbars(win, st);
      return true;
    }

    case evDestroy:
      // win->userdata freed by the framework via allocate_window_data.
      return false;

    case evSetFocus:
      return false;

    case evResize: {
      if (!s) return false;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      return false;
    }

    case evHScroll: {
      if (!s) return false;
      s->pan_x = (int)wparam;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      invalidate_window(win);
      return true;
    }

    case evVScroll: {
      if (!s) return false;
      s->pan_y = (int)wparam;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      invalidate_window(win);
      return true;
    }

    case evPaint: {
      if (!s || !doc) return true;

      // Dark workspace background
      fill_rect(get_sys_color(kColorWorkspaceBg),
                win->frame.x, win->frame.y, win->frame.w, win->frame.h);

      // Form surface (window-colored rectangle with a 1px dark border)
      int fx = win->frame.x + CANVAS_PADDING - s->pan_x;
      int fy = win->frame.y + CANVAS_PADDING - s->pan_y;
      int fw = doc->form_w;
      int fh = doc->form_h;
      fill_rect(get_sys_color(kColorWindowBg), fx, fy, fw, fh);
      fill_rect(get_sys_color(kColorDarkEdge), fx - 1, fy - 1, fw + 2, 1);
      fill_rect(get_sys_color(kColorDarkEdge), fx - 1, fy - 1, 1, fh + 2);
      fill_rect(get_sys_color(kColorDarkEdge), fx - 1, fy + fh, fw + 2, 1);
      fill_rect(get_sys_color(kColorDarkEdge), fx + fw, fy - 1, 1, fh + 2);

      // Draw all elements
      for (int i = 0; i < doc->element_count; i++)
        draw_element(win, s, &doc->elements[i]);

      // Selection handles
      draw_handles(win, s);

      // Rubber-band rectangle
      draw_rubber_band(win, s);

      return true;
    }

    case evLeftButtonDown: {
      if (!s || !doc) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int tool = g_app ? g_app->current_tool : ID_TOOL_SELECT;

      if (tool == ID_TOOL_SELECT) {
        // Check resize handles first
        int handle = hit_test_handles(s, lx, ly);
        if (handle >= 0 && s->selected_idx >= 0) {
          form_element_t *el = &doc->elements[s->selected_idx];
          s->drag_mode       = DRAG_RESIZE;
          s->drag_handle     = handle;
          s->drag_start      = (point_t){lx, ly};
          s->snap_x          = el->x;
          s->snap_y          = el->y;
          s->snap_w          = el->w;
          s->snap_h          = el->h;
          set_capture(win);
          return true;
        }
        // Hit test elements
        int hit = hit_test_elements(s, lx, ly);
        s->selected_idx = hit;
        if (hit >= 0) {
          form_element_t *el = &doc->elements[hit];
          s->drag_mode   = DRAG_MOVE;
          s->drag_start  = (point_t){lx, ly};
          s->snap_x      = el->x;
          s->snap_y      = el->y;
          set_capture(win);
        } else {
          s->drag_mode = DRAG_NONE;
        }
        invalidate_window(win);
        return true;
      }

      // Placement tools: start rubber-band drag
      int ctrl_type = tool_to_ctrl_type(tool);
      if (ctrl_type >= 0) {
        int fx = local_to_form_x(s, lx);
        int fy = local_to_form_y(s, ly);
        // Clamp to form surface
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;
        if (fx > doc->form_w) fx = doc->form_w;
        if (fy > doc->form_h) fy = doc->form_h;
        s->drag_mode   = DRAG_RUBBERBND;
        s->drag_start  = (point_t){lx, ly};
        s->rb_x        = fx;
        s->rb_y        = fy;
        s->rb_w        = 0;
        s->rb_h        = 0;
        set_capture(win);
        return true;
      }
      return false;
    }

    case evMouseMove: {
      if (!s) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      if (s->drag_mode == DRAG_MOVE && s->selected_idx >= 0) {
        form_element_t *el = &doc->elements[s->selected_idx];
        int nx = s->snap_x + (lx - s->drag_start.x);
        int ny = s->snap_y + (ly - s->drag_start.y);
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + el->w > doc->form_w) nx = doc->form_w - el->w;
        if (ny + el->h > doc->form_h) ny = doc->form_h - el->h;
        el->x = nx;
        el->y = ny;
        invalidate_window(win);
        return true;
      }

      if (s->drag_mode == DRAG_RESIZE && s->selected_idx >= 0) {
        int dx = lx - s->drag_start.x;
        int dy = ly - s->drag_start.y;
        canvas_apply_resize(s, dx, dy);
        doc->modified = true;
        invalidate_window(win);
        return true;
      }

      if (s->drag_mode == DRAG_RUBBERBND) {
        int fx = local_to_form_x(s, lx);
        int fy = local_to_form_y(s, ly);
        int ox = local_to_form_x(s, s->drag_start.x);
        int oy = local_to_form_y(s, s->drag_start.y);
        // Derive top-left + positive size
        int x0 = ox < fx ? ox : fx;
        int y0 = oy < fy ? oy : fy;
        s->rb_x = x0;
        s->rb_y = y0;
        s->rb_w = (ox < fx ? fx - ox : ox - fx);
        s->rb_h = (oy < fy ? fy - oy : oy - fy);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case evLeftButtonUp: {
      if (!s) return false;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      (void)lx; (void)ly;

      if (s->drag_mode == DRAG_MOVE && s->selected_idx >= 0) {
        doc->modified = true;
        form_doc_update_title(doc);
      }

      if (s->drag_mode == DRAG_RESIZE) {
        doc->modified = true;
        form_doc_update_title(doc);
      }

      if (s->drag_mode == DRAG_RUBBERBND) {
        int tool = g_app ? g_app->current_tool : ID_TOOL_SELECT;
        int ctrl_type = tool_to_ctrl_type(tool);
        int w = s->rb_w;
        int h = s->rb_h;
        if (ctrl_type >= 0) {
          // If no drag (click only), use the default size for the control.
          if (w < MIN_ELEM_W || h < MIN_ELEM_H)
            default_ctrl_size(ctrl_type, &w, &h);
          int x = s->rb_x;
          int y = s->rb_y;
          clamp_to_form(doc, &x, &y, &w, &h);
          canvas_add_element(doc, ctrl_type, x, y, w, h);
          s->selected_idx = doc->element_count - 1;
        }
        // Revert to Select tool after placing
        if (g_app) {
          g_app->current_tool = ID_TOOL_SELECT;
          if (g_app->tool_win)
            send_message(g_app->tool_win, tbSetActiveButton,
                         (uint32_t)ID_TOOL_SELECT, NULL);
        }
      }

      s->drag_mode = DRAG_NONE;
      s->drag_handle = -1;
      set_capture(NULL);
      invalidate_window(win);
      return true;
    }

    case evKeyDown: {
      if (!s) return false;
      // Del key deletes the selected element (redundant with accelerator,
      // but handles canvas-focused case when menubar proc isn't active).
      uint32_t key = wparam;
      if ((key == AX_KEY_DEL || key == AX_KEY_BACKSPACE) && s->selected_idx >= 0) {
        handle_menu_command(ID_EDIT_DELETE);
        return true;
      }
      return false;
    }

    case evWheel: {
      if (!s) return false;
      int delta = (int)(int16_t)LOWORD(wparam);
      s->pan_y -= delta * SCROLL_SENSITIVITY;
      canvas_clamp_pan(s, win->frame.w, win->frame.h);
      canvas_sync_scrollbars(win, s);
      invalidate_window(win);
      return true;
    }

    default:
      return false;
  }
}
