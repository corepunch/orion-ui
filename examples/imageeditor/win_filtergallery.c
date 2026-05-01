// Filter gallery dialog: one-shot browser for the Photo menu shader presets.

#include "imageeditor.h"

#define FG_CLIENT_W       560
#define FG_CLIENT_H       360
#define FG_LEFT_X          14
#define FG_LEFT_Y          18
#define FG_MAIN_SIZE      248
#define FG_LIST_X         286
#define FG_LIST_Y          18
#define FG_LIST_W         256
#define FG_LIST_H         290
#if UI_WINDOW_SCALE > 1
#define FG_ICON_SIZE       64
#define FG_ICON_CELL_W     74
#else
#define FG_ICON_SIZE       32
#define FG_ICON_CELL_W     72
#endif
#define FG_ICON_PAD         8
#define FG_ICON_LABEL_GAP   4
#if UI_WINDOW_SCALE > 1
#define FG_WHEEL_MULT      16
#else
#define FG_WHEEL_MULT       8
#endif
#define FG_BUTTON_W        66
#define FG_BUTTON_GAP       8
#define FG_BUTTON_Y       326
#define FG_BTN_OK        5001
#define FG_BTN_CANCEL    5002
#define FG_PREVIEW_TEX    384

typedef struct {
  canvas_doc_t *doc;
  int selected;
  int scroll_y;
  uint32_t preview_tex;
  bool accepted;
} filter_gallery_state_t;

typedef struct {
  int view_w;
  int icon_size;
  int cell_w;
  int cell_h;
  int cols;
  int rows;
  int content_h;
} icon_grid_layout_t;

static result_t filter_gallery_list_proc(window_t *win, uint32_t msg,
                                         uint32_t wparam, void *lparam);

static int filter_gallery_list_hit_test(window_t *win, uint32_t wparam,
                                        filter_gallery_state_t *st) {
  if (!st || !g_app) return -1;
  int view_w = get_client_rect(win).w;
  icon_grid_layout_t grid = icon_grid_layout(view_w, g_app->filter_count);
  return icon_grid_hit_test(&grid,
                            (int)(int16_t)LOWORD(wparam),
                            (int)(int16_t)HIWORD(wparam),
                            st->scroll_y,
                            g_app->filter_count);
}

static void draw_outline(rect_t r, uint32_t col) {
  fill_rect(col, R(r.x, r.y, r.w, 1));
  fill_rect(col, R(r.x, r.y, 1, r.h));
  fill_rect(col, R(r.x, r.y + r.h - 1, r.w, 1));
  fill_rect(col, R(r.x + r.w - 1, r.y, 1, r.h));
}

static icon_grid_layout_t icon_grid_layout(int view_w, int item_count) {
  icon_grid_layout_t g;
  memset(&g, 0, sizeof(g));
  g.view_w = view_w;
  g.icon_size = FG_ICON_SIZE;
  g.cell_w = FG_ICON_CELL_W;
  g.cell_h = FG_ICON_SIZE + FG_ICON_LABEL_GAP + text_char_height(FONT_SMALL) + 10;
  int usable_w = MAX(1, view_w - FG_ICON_PAD * 2);
  g.cols = MAX(1, usable_w / g.cell_w);
  g.rows = item_count > 0 ? (item_count + g.cols - 1) / g.cols : 0;
  g.content_h = FG_ICON_PAD * 2 + g.rows * g.cell_h;
  return g;
}

static rect_t icon_grid_cell_rect(const icon_grid_layout_t *g, int index, int scroll_y) {
  int col = index % g->cols;
  int row = index / g->cols;
  int grid_w = g->cols * g->cell_w;
  int x0 = FG_ICON_PAD + MAX(0, (g->view_w - FG_ICON_PAD * 2 - grid_w) / 2);
  return R(x0 + col * g->cell_w, FG_ICON_PAD + row * g->cell_h - scroll_y,
           g->cell_w, g->cell_h);
}

static int icon_grid_hit_test(const icon_grid_layout_t *g, int x, int y,
                              int scroll_y, int item_count) {
  int grid_w = g->cols * g->cell_w;
  int x0 = FG_ICON_PAD + MAX(0, (g->view_w - FG_ICON_PAD * 2 - grid_w) / 2);
  int local_x = x - x0;
  int local_y = y + scroll_y - FG_ICON_PAD;
  if (local_x < 0 || local_y < 0) return -1;
  int col = local_x / g->cell_w;
  int row = local_y / g->cell_h;
  if (col < 0 || col >= g->cols || row < 0) return -1;
  int idx = row * g->cols + col;
  return (idx >= 0 && idx < item_count) ? idx : -1;
}

static uint32_t filter_gallery_make_preview_tex(canvas_doc_t *doc) {
  if (!doc || doc->active_layer < 0 || doc->active_layer >= doc->layer_count)
    return 0;
  layer_t *lay = doc->layers[doc->active_layer];
  if (!lay || !lay->pixels || doc->canvas_w <= 0 || doc->canvas_h <= 0)
    return 0;

  const int dst = FG_PREVIEW_TEX;
  uint8_t *pix = malloc((size_t)dst * dst * 4);
  if (!pix) return 0;

  float scale = fmaxf((float)dst / (float)doc->canvas_w,
                      (float)dst / (float)doc->canvas_h);
  for (int y = 0; y < dst; y++) {
    for (int x = 0; x < dst; x++) {
      float sx_f = ((float)x + 0.5f - (float)dst * 0.5f) / scale +
                   (float)doc->canvas_w * 0.5f;
      float sy_f = ((float)y + 0.5f - (float)dst * 0.5f) / scale +
                   (float)doc->canvas_h * 0.5f;
      int sx = (int)floorf(sx_f);
      int sy = (int)floorf(sy_f);
      if (sx < 0) sx = 0;
      if (sy < 0) sy = 0;
      if (sx >= doc->canvas_w) sx = doc->canvas_w - 1;
      if (sy >= doc->canvas_h) sy = doc->canvas_h - 1;
      memcpy(&pix[((size_t)y * dst + x) * 4],
             &lay->pixels[((size_t)sy * doc->canvas_w + sx) * 4],
             4);
    }
  }

  uint32_t tex = R_CreateTextureRGBA(dst, dst, pix, R_FILTER_LINEAR, R_WRAP_CLAMP);
  free(pix);
  return tex;
}

static void filter_gallery_sync_scrollbar(window_t *list) {
  if (!list || !g_app) return;
  int view_w = get_client_rect(list).w;
  icon_grid_layout_t grid = icon_grid_layout(view_w, g_app->filter_count);
  scroll_info_t si;
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = MAX(FG_LIST_H, grid.content_h);
  si.nPage = FG_LIST_H;
  si.nPos = ((filter_gallery_state_t *)list->userdata)->scroll_y;
  set_scroll_info(list, SB_VERT, &si, false);
}

static void filter_gallery_scroll_to(window_t *list, int scroll_y) {
  if (!list || !g_app) return;
  filter_gallery_state_t *st = (filter_gallery_state_t *)list->userdata;
  if (!st) return;
  int view_w = get_client_rect(list).w;
  icon_grid_layout_t grid = icon_grid_layout(view_w, g_app->filter_count);
  int max_scroll = MAX(0, grid.content_h - FG_LIST_H);
  st->scroll_y = MIN(MAX(scroll_y, 0), max_scroll);
  list->scroll[1] = (uint16_t)st->scroll_y;
  scroll_info_t si = { .fMask = SIF_POS, .nPos = st->scroll_y };
  set_scroll_info(list, SB_VERT, &si, false);
  invalidate_window(list);
}

static void draw_filter_preview(filter_gallery_state_t *st, int filter_idx, rect_t r) {
  draw_checkerboard(r, 8);
  if (!st || !st->preview_tex) {
    fill_rect(get_sys_color(brWorkspaceBg), r);
    return;
  }
  if (filter_idx >= 0 && g_app && filter_idx < g_app->filter_count)
    draw_program_rect((int)st->preview_tex, r, g_app->filters[filter_idx].program, 1.0f);
  else
    draw_rect((int)st->preview_tex, r);
  draw_outline(r, get_sys_color(brBorderActive));
}

static result_t filter_gallery_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
  filter_gallery_state_t *st = (filter_gallery_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      st = (filter_gallery_state_t *)lparam;
      win->userdata = st;
      st->selected = (g_app && g_app->filter_count > 0) ? 0 : -1;
      st->preview_tex = filter_gallery_make_preview_tex(st->doc);

      window_t *list = create_window("",
                                     WINDOW_NOTITLE | WINDOW_VSCROLL | WINDOW_NORESIZE,
                                     MAKERECT(FG_LIST_X, FG_LIST_Y, FG_LIST_W, FG_LIST_H),
                                     win, filter_gallery_list_proc, 0, st);
      if (list) {
        list->id = 4001;
        filter_gallery_sync_scrollbar(list);
      }

      window_t *ok = create_window("OK", BUTTON_DEFAULT,
                                   MAKERECT(FG_CLIENT_W - 2 * FG_BUTTON_W - FG_BUTTON_GAP - 16,
                                            FG_BUTTON_Y, FG_BUTTON_W, BUTTON_HEIGHT),
                                   win, win_button, 0, NULL);
      if (ok) ok->id = FG_BTN_OK;
      window_t *cancel = create_window("Cancel", 0,
                                       MAKERECT(FG_CLIENT_W - FG_BUTTON_W - 16,
                                                FG_BUTTON_Y, FG_BUTTON_W, BUTTON_HEIGHT),
                                       win, win_button, 0, NULL);
      if (cancel) cancel->id = FG_BTN_CANCEL;
      return true;
    }

    case evPaint: {
      rect_t preview = R(FG_LEFT_X, FG_LEFT_Y, FG_MAIN_SIZE, FG_MAIN_SIZE);
      fill_rect(get_sys_color(brWindowDarkBg), rect_inset(preview, -2));
      draw_filter_preview(st, st ? st->selected : -1, preview);
      if (st && st->selected >= 0 && g_app && st->selected < g_app->filter_count) {
        rect_t name_r = R(FG_LEFT_X, FG_LEFT_Y + FG_MAIN_SIZE + 10,
                          FG_MAIN_SIZE, CONTROL_HEIGHT);
        draw_text_clipped(FONT_SMALL, g_app->filters[st->selected].name, &name_r,
                          get_sys_color(brTextNormal), TEXT_ALIGN_CENTER);
      } else {
        rect_t msg_r = R(FG_LEFT_X, FG_LEFT_Y + FG_MAIN_SIZE + 10,
                         FG_MAIN_SIZE, CONTROL_HEIGHT);
        draw_text_clipped(FONT_SMALL, "No filters loaded", &msg_r,
                          get_sys_color(brTextDisabled), TEXT_ALIGN_CENTER);
      }
      return false;
    }

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        uint16_t id = LOWORD(wparam);
        if (id == FG_BTN_OK) {
          if (st && st->doc && st->selected >= 0) {
            doc_push_undo(st->doc);
            if (!imageeditor_apply_filter(st->doc, st->selected)) {
              doc_discard_undo(st->doc);
              end_dialog(win, 0);
              return true;
            }
            st->accepted = true;
          }
          end_dialog(win, 1);
          return true;
        }
        if (id == FG_BTN_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    case evDestroy:
      if (st && st->preview_tex) {
        R_DeleteTexture(st->preview_tex);
        st->preview_tex = 0;
      }
      return false;
  }

  return false;
}

static result_t filter_gallery_list_proc(window_t *win, uint32_t msg,
                                         uint32_t wparam, void *lparam) {
  filter_gallery_state_t *st = (filter_gallery_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      return true;

    case evVScroll:
      filter_gallery_scroll_to(win, (int)wparam);
      return true;

    case evWheel:
      if (!st) return false;
      filter_gallery_scroll_to(win, st->scroll_y - (int)(int16_t)HIWORD(wparam) * FG_WHEEL_MULT);
      return true;

    case evPaint: {
      int count = g_app ? g_app->filter_count : 0;
      rect_t cr = get_client_rect(win);
      int view_w = cr.w;
      icon_grid_layout_t grid = icon_grid_layout(view_w, count);
      fill_rect(get_sys_color(brWindowBg), R(0, 0, view_w, cr.h));
      for (int i = 0; i < count; i++) {
        rect_t cell = icon_grid_cell_rect(&grid, i, st ? st->scroll_y : 0);
        if (cell.y + cell.h < 0 || cell.y >= cr.h) continue;
        bool selected = st && i == st->selected;
        rect_t icon = R(cell.x + (cell.w - grid.icon_size) / 2, cell.y + 4,
                        grid.icon_size, grid.icon_size);
        rect_t label = R(cell.x + 2, icon.y + icon.h + FG_ICON_LABEL_GAP,
                         cell.w - 4, text_char_height(FONT_SMALL) + 2);
        if (selected)
          fill_rect(get_sys_color(brActiveTitlebar),
                    rect_inset(R(cell.x + 2, icon.y - 2,
                                 cell.w - 4, icon.h + FG_ICON_LABEL_GAP + label.h + 4), -1));
        draw_filter_preview(st, i, icon);
        draw_text_clipped(FONT_SMALL, g_app->filters[i].name, &label,
                          get_sys_color(selected ? brActiveTitlebarText : brTextNormal),
                          TEXT_ALIGN_CENTER);
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!st) return true;
      int idx = filter_gallery_list_hit_test(win, wparam, st);
      if (idx >= 0) {
        st->selected = idx;
        invalidate_window(win);
        if (win->parent) invalidate_window(win->parent);
      }
      return true;
    }

    case evLeftButtonDoubleClick: {
      if (!st) return true;
      int idx = filter_gallery_list_hit_test(win, wparam, st);
      if (idx >= 0 && win->parent) {
        st->selected = idx;
        send_message(win->parent, evCommand,
                     MAKEWPARAM(FG_BTN_OK, btnClicked), win);
      }
      return true;
    }
  }

  return false;
}

bool show_filter_gallery_dialog(window_t *parent) {
  if (!g_app || !g_app->active_doc) return false;
  canvas_doc_t *doc = g_app->active_doc;
  if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count)
    return false;

  filter_gallery_state_t st;
  memset(&st, 0, sizeof(st));
  st.doc = doc;
  st.selected = -1;

  rect_t wr = {0, 0, FG_CLIENT_W, FG_CLIENT_H};
  adjust_window_rect(&wr, WINDOW_DIALOG | WINDOW_NOTRAYBUTTON);
  uint32_t res = show_dialog_ex("Filter Gallery", wr.w, wr.h, parent,
                                WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                filter_gallery_proc, &st);
  if (res && st.accepted && doc->win && st.selected >= 0 && st.selected < g_app->filter_count) {
    send_message(doc->win, evStatusBar, 0, (void *)g_app->filters[st.selected].name);
  }
  return res && st.accepted;
}
