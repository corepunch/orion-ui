// Layers palette window for the image editor.
//
// Shows the document's layer stack (topmost layer at the top of the list).
// Each row exposes:
//   - Eye icon toggle (visibility)
//   - Mask chip indicator / mask-editing toggle
//   - Layer name
//
// A button strip at the bottom provides: New, Dup, Del, Move Up, Move Down.
//
// The palette calls layer management APIs directly (via g_app->active_doc)
// and refreshes itself after each change.

#include "imageeditor.h"

// ============================================================
// Drawing helpers
// ============================================================

// Color palette used by the layers panel.
#define COL_ROW_ACTIVE  MAKE_COLOR(0x00, 0x78, 0xD7, 0xFF)  // blue highlight
#define COL_ROW_HOVER   MAKE_COLOR(0xCC, 0xE4, 0xF7, 0xFF)  // light hover
#define COL_MASK_ON     MAKE_COLOR(0x40, 0x40, 0x40, 0xFF)  // dark text when mask
#define COL_MASK_EDIT   MAKE_COLOR(0xE0, 0x40, 0x00, 0xFF)  // orange = editing mask

// Indices of the 5 bottom buttons (New, Duplicate, Delete, Up, Down).
enum {
  LBTN_NEW = 0,
  LBTN_DUP,
  LBTN_DEL,
  LBTN_UP,
  LBTN_DOWN,
  LBTN_COUNT
};

static const char *const kBtnLabels[LBTN_COUNT] = { "+", "Dup", "-", "Up", "Dn" };

// ============================================================
// State
// ============================================================

typedef struct {
  int hover_row;    // visual row under cursor (-1 = none)
  int hover_btn;    // button index under cursor (-1 = none)
  int press_btn;    // button currently held (-1 = none)
  int scroll_top;   // first visible row (for >LAYERS_MAX_VIS_ROWS layers)
} layers_win_state_t;

// ============================================================
// Layout helpers
// ============================================================

// Number of rows that fit in the current window height.
static int visible_rows(window_t *win) {
  int list_h = win->frame.h - LAYERS_BTN_STRIP_H;
  if (list_h < 0) list_h = 0;
  return list_h / LAYERS_ROW_H;
}

// y-coordinate (in client space) of the top of a visual row.
static int row_y(int row) { return row * LAYERS_ROW_H; }

// Convert a visual row to a layer index (0=bottom, count-1=top).
// Visual row 0 = topmost layer (count-1), row 1 = count-2, etc.
static int row_to_layer_idx(const canvas_doc_t *doc, int row) {
  return doc->layer_count - 1 - row;
}

// Convert a layer index to the visual row.
static int layer_idx_to_row(const canvas_doc_t *doc, int idx) {
  return doc->layer_count - 1 - idx;
}

// The button strip occupies the bottom LAYERS_BTN_STRIP_H pixels.
static int btn_strip_y(window_t *win) {
  return win->frame.h - LAYERS_BTN_STRIP_H;
}

// Hit-test: returns button index or -1.
static int hit_btn(window_t *win, int mx, int my) {
  int by = btn_strip_y(win);
  if (my < by || my >= by + LAYERS_BTN_STRIP_H) return -1;
  int bw = win->frame.w / LBTN_COUNT;
  if (bw < 1) return -1;
  int i = mx / bw;
  if (i < 0) i = 0;
  if (i >= LBTN_COUNT) i = LBTN_COUNT - 1;
  return i;
}

// Hit-test: returns visual row index or -1.
static int hit_row(window_t *win, int mx, int my) {
  (void)mx;
  int strip = btn_strip_y(win);
  if (my < 0 || my >= strip) return -1;
  return my / LAYERS_ROW_H;
}

// Hit-test within a row: which sub-zone?
enum { ZONE_EYE, ZONE_CHIP, ZONE_NAME };
static int hit_zone(int mx) {
  if (mx >= 1 && mx < 1 + LAYERS_EYE_W) return ZONE_EYE;
  if (mx >= 1 + LAYERS_EYE_W + 2 &&
      mx < 1 + LAYERS_EYE_W + 2 + LAYERS_CHIP_W) return ZONE_CHIP;
  return ZONE_NAME;
}

// ============================================================
// Painting
// ============================================================

static void paint_layers(window_t *win, layers_win_state_t *st) {
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  int w = win->frame.w;
  int strip_y = btn_strip_y(win);

  // Background of the list area.
  fill_rect(get_sys_color(brWindowBg), R(0, 0, w, strip_y));

  if (doc && doc->layer_count > 0) {
    int nvis = visible_rows(win);
    for (int row = 0; row < nvis; row++) {
      int li = row_to_layer_idx(doc, row + st->scroll_top);
      if (li < 0 || li >= doc->layer_count) break;
      const layer_t *lay = doc->layers[li];
      int ry = row_y(row);

      // Row background.
      uint32_t bg;
      if (li == doc->active_layer)
        bg = COL_ROW_ACTIVE;
      else if (row + st->scroll_top == st->hover_row)
        bg = COL_ROW_HOVER;
      else
        bg = get_sys_color(brWindowBg);
      fill_rect(bg, R(0, ry, w, LAYERS_ROW_H));

      // Eye icon: small "E" or "-" character.
      uint32_t eye_col = (li == doc->active_layer)
                         ? MAKE_COLOR(0xFF,0xFF,0xFF,0xFF)
                         : get_sys_color(brTextNormal);
      draw_text_small(lay->visible ? "E" : "-",
                      1 + (LAYERS_EYE_W - text_strwidth(FONT_SMALL, "E")) / 2,
                      ry + (LAYERS_ROW_H - FONT_SIZE_SMALL) / 2, eye_col);

      // Mask chip: show "M" if layer has a mask; orange if currently editing.
      if (lay->mask) {
        uint32_t chip_col = (doc->editing_mask && li == doc->active_layer)
                            ? COL_MASK_EDIT : COL_MASK_ON;
        int chip_x = 1 + LAYERS_EYE_W + 2;
        fill_rect(chip_col, R(chip_x, ry + 3, LAYERS_CHIP_W, LAYERS_ROW_H - 6));
        uint32_t label_col = MAKE_COLOR(0xFF,0xFF,0xFF,0xFF);
        draw_text_small("M",
                        chip_x + (LAYERS_CHIP_W - text_strwidth(FONT_SMALL, "M")) / 2,
                        ry + (LAYERS_ROW_H - FONT_SIZE_SMALL) / 2, label_col);
      }

      // Layer name (truncate if too wide).
      uint32_t name_col = (li == doc->active_layer)
                          ? MAKE_COLOR(0xFF,0xFF,0xFF,0xFF)
                          : get_sys_color(brTextNormal);
      draw_text_small(lay->name, LAYERS_NAME_X,
                      ry + (LAYERS_ROW_H - FONT_SIZE_SMALL) / 2, name_col);

      // Separator line.
      fill_rect(get_sys_color(brWindowDarkBg), R(0, ry + LAYERS_ROW_H - 1, w, 1));
    }
  } else {
    // No document open.
    uint32_t hint = get_sys_color(brTextDisabled);
    draw_text_small("(no document)",
                    4, 4 + (LAYERS_ROW_H - FONT_SIZE_SMALL) / 2, hint);
  }

  // Separator between list and button strip.
  fill_rect(get_sys_color(brWindowDarkBg), R(0, strip_y, w, 1));

  // Button strip background.
  fill_rect(get_sys_color(brWindowBg), R(0, strip_y + 1, w, LAYERS_BTN_STRIP_H - 1));

  // Draw buttons.
  int bw = w / LBTN_COUNT;
  for (int i = 0; i < LBTN_COUNT; i++) {
    int bx = i * bw;
    int by = strip_y + 2;
    int bh = LAYERS_BTN_STRIP_H - 4;
    bool pressed = (st->press_btn == i);
    bool hovering = (st->hover_btn == i && st->press_btn < 0);
    uint32_t btn_bg = pressed    ? get_sys_color(brWindowDarkBg)
                    : hovering   ? COL_ROW_HOVER
                                 : get_sys_color(brWindowBg);
    fill_rect(btn_bg, R(bx, by, bw, bh));
    // Simple 1px border using four fill_rect calls.
    uint32_t border = get_sys_color(brDarkEdge);
    fill_rect(border, R(bx, by, bw, 1));
    fill_rect(border, R(bx, by + bh - 1, bw, 1));
    fill_rect(border, R(bx, by, 1, bh));
    fill_rect(border, R(bx + bw - 1, by, 1, bh));
    int tx = bx + (bw - text_strwidth(FONT_SMALL, kBtnLabels[i])) / 2;
    int ty = by + (bh - FONT_SIZE_SMALL) / 2;
    draw_text_small(kBtnLabels[i], tx, ty, get_sys_color(brTextNormal));
  }
}

// ============================================================
// Action dispatch
// ============================================================

static void layers_do_btn(int btn_idx) {
  if (!g_app || !g_app->active_doc) return;
  canvas_doc_t *doc = g_app->active_doc;
  switch (btn_idx) {
    case LBTN_NEW:
      doc_push_undo(doc);
      if (!doc_add_layer(doc)) { doc_discard_undo(doc); return; }
      break;
    case LBTN_DUP:
      doc_push_undo(doc);
      if (!doc_duplicate_layer(doc)) { doc_discard_undo(doc); return; }
      break;
    case LBTN_DEL:
      doc_push_undo(doc);
      if (!doc_delete_layer(doc)) { doc_discard_undo(doc); return; }
      break;
    case LBTN_UP:
      doc_push_undo(doc);
      doc_move_layer_up(doc);
      break;
    case LBTN_DOWN:
      doc_push_undo(doc);
      doc_move_layer_down(doc);
      break;
  }
  invalidate_window(doc->canvas_win);
  layers_win_refresh();
}

// ============================================================
// Window procedure
// ============================================================

result_t win_layers_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  layers_win_state_t *st = (layers_win_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      layers_win_state_t *s = allocate_window_data(win, sizeof(layers_win_state_t));
      s->hover_row = -1;
      s->hover_btn = -1;
      s->press_btn = -1;
      s->scroll_top = 0;
      return true;
    }

    case evDestroy:
      if (g_app) g_app->layers_win = NULL;
      return false;

    case evPaint:
      if (st) paint_layers(win, st);
      return true;

    case evMouseMove: {
      if (!st) return false;
      int mx = LOWORD(wparam);
      int my = HIWORD(wparam);
      int new_row = hit_row(win, mx, my);
      int new_btn = hit_btn(win, mx, my);
      if (new_row != st->hover_row || new_btn != st->hover_btn) {
        st->hover_row = new_row;
        st->hover_btn = new_btn;
        invalidate_window(win);
      }
      return true;
    }

    case evMouseLeave:
      if (st && (st->hover_row >= 0 || st->hover_btn >= 0)) {
        st->hover_row = -1;
        st->hover_btn = -1;
        invalidate_window(win);
      }
      return false;

    case evLeftButtonDown: {
      if (!st || !g_app || !g_app->active_doc) return false;
      canvas_doc_t *doc = g_app->active_doc;
      int mx = LOWORD(wparam);
      int my = HIWORD(wparam);

      int btn = hit_btn(win, mx, my);
      if (btn >= 0) {
        st->press_btn = btn;
        invalidate_window(win);
        return true;
      }

      int row = hit_row(win, mx, my);
      if (row >= 0) {
        int li = row_to_layer_idx(doc, row + st->scroll_top);
        if (li < 0 || li >= doc->layer_count) return true;
        int zone = hit_zone(mx);
        if (zone == ZONE_EYE) {
          doc->layers[li]->visible = !doc->layers[li]->visible;
          doc->canvas_dirty = true;
          doc->modified = true;
          invalidate_window(doc->canvas_win);
          invalidate_window(win);
        } else if (zone == ZONE_CHIP && doc->layers[li]->mask) {
          if (li == doc->active_layer) {
            doc->editing_mask = !doc->editing_mask;
          } else {
            doc_set_active_layer(doc, li);
            doc->editing_mask = true;
          }
          invalidate_window(win);
        } else {
          // Select layer.
          if (li != doc->active_layer) {
            doc_set_active_layer(doc, li);
            invalidate_window(doc->canvas_win);
            invalidate_window(win);
          }
        }
        return true;
      }
      return false;
    }

    case evLeftButtonUp: {
      if (!st) return false;
      if (st->press_btn >= 0) {
        int btn = st->press_btn;
        st->press_btn = -1;
        // Only fire if cursor is still over the same button.
        int mx = LOWORD(wparam);
        int my = HIWORD(wparam);
        if (hit_btn(win, mx, my) == btn)
          layers_do_btn(btn);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case evWheel: {
      if (!st || !g_app || !g_app->active_doc) return false;
      canvas_doc_t *doc = g_app->active_doc;
      int delta = (int)(int32_t)wparam;
      int nvis = visible_rows(win);
      int max_scroll = MAX(0, doc->layer_count - nvis);
      st->scroll_top += (delta > 0) ? -1 : 1;
      if (st->scroll_top < 0) st->scroll_top = 0;
      if (st->scroll_top > max_scroll) st->scroll_top = max_scroll;
      invalidate_window(win);
      return true;
    }

    case evResize:
      if (st) {
        st->hover_row = -1;
        st->hover_btn = -1;
      }
      return false;

    default:
      return false;
  }
}

// ============================================================
// Factory and refresh helpers
// ============================================================

window_t *create_layers_window(void) {
  if (!g_app) return NULL;
  int win_h = LAYERS_WIN_H;
  window_t *win = create_window(
      "Layers",
      WINDOW_ALWAYSONTOP | WINDOW_NORESIZE | WINDOW_NOTRAYBUTTON,
      MAKERECT(LAYERS_WIN_X, LAYERS_WIN_Y, LAYERS_WIN_W, win_h),
      NULL, win_layers_proc, 0, NULL);
  if (!win) return NULL;
  show_window(win, true);
  g_app->layers_win = win;
  return win;
}

void layers_win_refresh(void) {
  if (!g_app || !g_app->layers_win) return;
  layers_win_state_t *st = (layers_win_state_t *)g_app->layers_win->userdata;
  if (st && g_app->active_doc) {
    // Ensure scroll_top keeps the active layer visible.
    canvas_doc_t *doc = g_app->active_doc;
    int nvis = visible_rows(g_app->layers_win);
    int active_row = layer_idx_to_row(doc, doc->active_layer);
    if (active_row < st->scroll_top)
      st->scroll_top = active_row;
    else if (active_row >= st->scroll_top + nvis)
      st->scroll_top = active_row - nvis + 1;
    if (st->scroll_top < 0) st->scroll_top = 0;
  }
  invalidate_window(g_app->layers_win);
}
