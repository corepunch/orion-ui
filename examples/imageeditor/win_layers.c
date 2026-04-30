// Layers palette window for the image editor.
//
// Shows the document's layer stack (topmost layer at the top of the list).
// Each row exposes:
//   - Eye icon toggle (visibility)
//   - Alpha edit icon toggle
//   - Layer name
//
// A WINDOW_TOOLBAR at the top provides: New, Duplicate, Delete, Move Up,
// Move Down via sysicon_* icons.  The toolbar fires tbButtonClick with the
// corresponding ID_LAYER_* command ident, which is forwarded to
// handle_menu_command() — the same handler used by the Layer menu.
//
// The palette calls layer management APIs directly (via g_app->active_doc)
// and refreshes itself after each change.

#include "imageeditor.h"

// ============================================================
// Drawing helpers
// ============================================================

// Color palette used by the layers panel.
#define COL_ROW_ACTIVE  MAKE_COLOR(0x00, 0x78, 0xD7, 0xFF)  // blue highlight
#define COL_ALPHA_EDIT  MAKE_COLOR(0xE0, 0x40, 0x00, 0xFF)  // orange = editing alpha

// ============================================================
// Toolbar definition
// ============================================================

// Toolbar definition: { type, ident, icon, w, flags, tooltip }
static const toolbar_item_t kLayersToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, ID_LAYER_NEW,       sysicon_image_add,  0, 0, "New"     },
  { TOOLBAR_ITEM_BUTTON, ID_LAYER_DUPLICATE, sysicon_page_copy,  0, 0, "Dup"     },
  { TOOLBAR_ITEM_BUTTON, ID_LAYER_DELETE,    sysicon_delete,     0, 0, "Delete"  },
  { TOOLBAR_ITEM_SPACER, 0, 0, 0, 0, NULL },
  { TOOLBAR_ITEM_BUTTON, ID_LAYER_MOVE_UP,   sysicon_arrow_up,   0, 0, "Up"      },
  { TOOLBAR_ITEM_BUTTON, ID_LAYER_MOVE_DOWN, sysicon_arrow_down, 0, 0, "Down"    },
};

// ============================================================
// State
// ============================================================

typedef struct {
  int scroll_top;   // first visible row (for >LAYERS_MAX_VIS_ROWS layers)
  window_t *blend_cb[LAYERS_MAX_VIS_ROWS];
} layers_win_state_t;

static const char *kBlendNames[LAYER_BLEND_COUNT] = {
  "Normal",
  "Multiply",
  "Screen",
  "Add",
};

// ============================================================
// Layout helpers
// ============================================================

static rect_t layers_client_rect(window_t *win) {
  return get_client_rect(win);
}

// Number of rows that fit in the current client height.
// get_client_rect() already subtracts non-client chrome and toolbar band.
static int visible_rows(window_t *win) {
  rect_t cr = layers_client_rect(win);
  int list_h = cr.h;
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
// Hit-test: returns visual row index or -1.
static int hit_row(window_t *win, int mx, int my) {
  (void)mx;
  int client_h = layers_client_rect(win).h;
  if (my < 0 || my >= client_h) return -1;
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

static void blend_combo_fill(window_t *cb) {
  if (!cb) return;
  send_message(cb, cbClear, 0, NULL);
  for (int i = 0; i < LAYER_BLEND_COUNT; i++)
    send_message(cb, cbAddString, 0, (void *)kBlendNames[i]);
}

static void sync_blend_combo(window_t *win, layers_win_state_t *st, int row) {
  if (!win || !st || row < 0 || row >= LAYERS_MAX_VIS_ROWS) return;
  window_t *cb = st->blend_cb[row];
  if (!cb) return;
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  if (!doc) {
    cb->visible = false;
    return;
  }

  int li = row_to_layer_idx(doc, row + st->scroll_top);
  if (li < 0 || li >= doc->layer_count) {
    cb->visible = false;
    return;
  }

  const layer_t *lay = doc->layers[li];
  if (!lay) {
    cb->visible = false;
    return;
  }

  int nvis = visible_rows(win);
  if (row >= nvis) {
    cb->visible = false;
    return;
  }

  rect_t frame = R(LAYERS_BLEND_X, row_y(row) + 3, LAYERS_BLEND_W, LAYERS_ROW_H - 6);
  cb->frame = frame;
  cb->visible = true;
  send_message(cb, cbSetCurrentSelection, lay->blend_mode, NULL);
  invalidate_window(cb);
}

static void sync_all_blend_combos(window_t *win, layers_win_state_t *st) {
  for (int row = 0; row < LAYERS_MAX_VIS_ROWS; row++)
    sync_blend_combo(win, st, row);
}

// ============================================================
// Painting
// ============================================================

static void paint_layers(window_t *win, layers_win_state_t *st) {
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  rect_t cr = layers_client_rect(win);
  int w = cr.w;
  int client_h = cr.h;

  // Background of the list area.
  fill_rect(get_sys_color(brWindowBg), R(0, 0, w, client_h));

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
      else
        bg = get_sys_color(brWindowBg);
      fill_rect(bg, R(0, ry, w, LAYERS_ROW_H));

      // Eye icon: visibility toggle.
      int icon_y = ry + (LAYERS_ROW_H - 16) / 2;
      // uint32_t eye_col = (li == doc->active_layer)
      //                    ? MAKE_COLOR(0xFF,0xFF,0xFF,0xFF)
      //                    : get_sys_color(brTextNormal);
      draw_icon16(lay->visible ? sysicon_eye_show : sysicon_eye_hide,
                  1, icon_y, lay->visible ? 0xffffffff : 0x40ffffff); //eye_col);

      // Alpha edit icon: pencil when editing, transparency icon when viewing.
      uint32_t chip_col = (doc->editing_mask && li == doc->active_layer)
                          ? COL_ALPHA_EDIT : get_sys_color(brTextNormal);
      int chip_x = 1 + LAYERS_EYE_W + 2;
      draw_icon16((doc->editing_mask && li == doc->active_layer)
                  ? sysicon_pencil : sysicon_transparency,
                  chip_x, icon_y, chip_col);

      // Layer name.
      uint32_t name_col = (li == doc->active_layer)
                          ? MAKE_COLOR(0xFF,0xFF,0xFF,0xFF)
                          : get_sys_color(brTextNormal);
      rect_t name_rect = R(LAYERS_NAME_X, ry, LAYERS_NAME_W, LAYERS_ROW_H);
      draw_text_clipped(FONT_SMALL, lay->name, &name_rect, name_col, TEXT_PADDING_LEFT);

      // Blend selector for this row.
      sync_blend_combo(win, st, row);

      // Separator line.
      fill_rect(get_sys_color(brWindowDarkBg), R(0, ry + LAYERS_ROW_H - 1, w, 1));
    }
  } else {
    // No document open.
    uint32_t hint = get_sys_color(brTextDisabled);
    draw_text_clipped(FONT_SMALL, "(no document)",
                      &R(4, 0, w - 8, client_h), hint, TEXT_PADDING_LEFT);
  }
}

// ============================================================
// Window procedure
// ============================================================

result_t win_layers_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  layers_win_state_t *st = (layers_win_state_t *)win->userdata;
  (void)lparam;
  switch (msg) {
    case evCreate: {
      layers_win_state_t *s = allocate_window_data(win, sizeof(layers_win_state_t));
      s->scroll_top = 0;
      for (int i = 0; i < LAYERS_MAX_VIS_ROWS; i++) {
        s->blend_cb[i] = create_window("Normal", 0,
                                       MAKERECT(LAYERS_BLEND_X, i * LAYERS_ROW_H + 3,
                                                LAYERS_BLEND_W, LAYERS_ROW_H - 6),
                                       win, win_combobox, 0, NULL);
        if (s->blend_cb[i]) {
          s->blend_cb[i]->id = ID_LAYER_BLEND_BASE + i;
          blend_combo_fill(s->blend_cb[i]);
          show_window(s->blend_cb[i], false);
        }
      }
      send_message(win, tbSetItems,
                   sizeof(kLayersToolbar) / sizeof(kLayersToolbar[0]),
                   (void *)kLayersToolbar);
      return true;
    }

    case evDestroy:
      if (g_app) g_app->layers_win = NULL;
      return false;

    case evPaint:
      if (st) {
        paint_layers(win, st);
        sync_all_blend_combos(win, st);
      }
      return true;

    case tbButtonClick:
      handle_menu_command((uint16_t)wparam);
      return true;

    case evLeftButtonDown: {
      if (!st || !g_app || !g_app->active_doc) return false;
      canvas_doc_t *doc = g_app->active_doc;
      int mx = LOWORD(wparam);
      int my = HIWORD(wparam);

      int row = hit_row(win, mx, my);
      if (row >= 0) {
        int li = row_to_layer_idx(doc, row + st->scroll_top);
        if (li < 0 || li >= doc->layer_count) return true;
        int zone = hit_zone(mx);
        if (zone == ZONE_EYE) {
          doc_push_undo(doc);
          doc->layers[li]->visible = !doc->layers[li]->visible;
          doc->canvas_dirty = true;
          doc->modified = true;
          invalidate_window(doc->canvas_win);
          invalidate_window(win);
        } else if (zone == ZONE_CHIP) {
          if (li == doc->active_layer) {
            doc->editing_mask = !doc->editing_mask;
            if (doc->canvas_win) invalidate_window(doc->canvas_win);
          } else {
            doc_set_active_layer(doc, li);
            doc->editing_mask = true;
            if (doc->canvas_win) invalidate_window(doc->canvas_win);
          }
          if (doc->canvas_win) {
            canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
            if (state) {
              canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                                       state->hover_valid);
            }
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

    case evCommand: {
      uint16_t code = HIWORD(wparam);
      uint16_t id = LOWORD(wparam);
      if (code == cbSelectionChange && st && g_app && g_app->active_doc) {
        canvas_doc_t *doc = g_app->active_doc;
        int row = id - ID_LAYER_BLEND_BASE;
        if (row >= 0 && row < LAYERS_MAX_VIS_ROWS) {
          int li = row_to_layer_idx(doc, row + st->scroll_top);
          if (li >= 0 && li < doc->layer_count && lparam) {
            result_t sel = send_message((window_t *)lparam, cbGetCurrentSelection, 0, NULL);
            if (sel != (result_t)kComboBoxError) {
              doc_push_undo(doc);
              doc_set_layer_blend_mode(doc, li, (layer_blend_mode_t)sel);
              invalidate_window(win);
              if (doc->canvas_win) invalidate_window(doc->canvas_win);
              return true;
            }
          }
        }
      }
      break;
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
      sync_all_blend_combos(win, st);
      return true;
    }

    case evResize:
      if (st) sync_all_blend_combos(win, st);
      return false;

    default:
      return false;
  }
  return false;
}

// ============================================================
// Factory and refresh helpers
// ============================================================

window_t *create_layers_window(void) {
  if (!g_app) return NULL;
  window_t *win = create_window(
      "Layers",
      WINDOW_TOOLBAR | WINDOW_ALWAYSONTOP | WINDOW_NORESIZE | WINDOW_NOTRAYBUTTON,
      MAKERECT(LAYERS_WIN_X, LAYERS_WIN_Y, LAYERS_WIN_W, LAYERS_WIN_H),
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
  if (st)
    sync_all_blend_combos(g_app->layers_win, st);
  invalidate_window(g_app->layers_win);
}
