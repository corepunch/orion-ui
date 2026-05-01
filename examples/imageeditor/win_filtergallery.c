// Filter gallery dialog: one-shot browser for the Photo menu shader presets.
// The thumbnail grid is rendered by win_reportview in RVM_VIEW_LARGE_ICON mode.
// Each thumbnail is baked once at dialog-open time and stored in a single GPU
// texture strip so no shader runs per frame during display.

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
#define FG_ICON_SIZE       64
#define FG_ICON_CELL_W     72
#endif
#define FG_BUTTON_W        66
#define FG_BUTTON_GAP       8
#define FG_BUTTON_Y       326
#define FG_BTN_OK        5001
#define FG_BTN_CANCEL    5002
#define FG_PREVIEW_TEX    384
#define FG_LIST_ID       4001

typedef struct {
  canvas_doc_t *doc;
  int selected;
  uint32_t preview_tex;
  uint32_t thumb_sheet_tex;   // GL texture owning the thumbnail strip pixels
  bitmap_strip_t thumb_strip; // strip descriptor pointing at thumb_sheet_tex
  bool accepted;
} filter_gallery_state_t;

static void draw_outline(irect16_t r, uint32_t col) {
  fill_rect(col, R(r.x, r.y, r.w, 1));
  fill_rect(col, R(r.x, r.y, 1, r.h));
  fill_rect(col, R(r.x, r.y + r.h - 1, r.w, 1));
  fill_rect(col, R(r.x + r.w - 1, r.y, 1, r.h));
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

// Bake a 64×64 thumbnail for every filter by rendering the preview texture
// through each filter's GL program once, reading the pixels back, and packing
// them into a single vertical strip texture.  The strip is stored in st and
// fed to win_reportview via RVM_SETICONSTRIP.
static void filter_gallery_bake_thumbnails(filter_gallery_state_t *st) {
  int count = g_app ? g_app->filter_count : 0;
  if (count <= 0 || !st->preview_tex) return;

  const int sz = FG_ICON_SIZE;
  size_t per   = (size_t)sz * sz * 4;
  uint8_t *sheet = calloc((size_t)count, per);
  if (!sheet) return;

  for (int i = 0; i < count; i++) {
    if (!g_app->filters[i].program) continue;
    uint32_t baked = 0;
    if (!bake_texture_program((int)st->preview_tex, sz, sz,
                              g_app->filters[i].program, 1.0f, &baked))
      continue;
    read_texture_rgba((int)baked, sz, sz, sheet + (size_t)i * per);
    R_DeleteTexture(baked);
  }

  st->thumb_sheet_tex = R_CreateTextureRGBA(sz, sz * count, sheet,
                                            R_FILTER_LINEAR, R_WRAP_CLAMP);
  free(sheet);
  if (!st->thumb_sheet_tex) return;

  st->thumb_strip.tex     = st->thumb_sheet_tex;
  st->thumb_strip.icon_w  = sz;
  st->thumb_strip.icon_h  = sz;
  st->thumb_strip.cols    = 1;
  st->thumb_strip.sheet_w = sz;
  st->thumb_strip.sheet_h = sz * count;
}

static void draw_filter_preview(filter_gallery_state_t *st, int filter_idx, irect16_t r) {
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

// Apply the currently-selected filter and close the dialog with success.
// Extracted so both the OK button and the double-click path share the same logic.
static void filter_gallery_accept(window_t *win, filter_gallery_state_t *st) {
  if (st && st->doc && st->selected >= 0) {
    doc_push_undo(st->doc);
    if (!imageeditor_apply_filter(st->doc, st->selected)) {
      doc_discard_undo(st->doc);
      end_dialog(win, 0);
      return;
    }
    st->accepted = true;
  }
  end_dialog(win, 1);
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
      filter_gallery_bake_thumbnails(st);

      window_t *list = create_window("",
                                     WINDOW_NOTITLE | WINDOW_NORESIZE,
                                     MAKERECT(FG_LIST_X, FG_LIST_Y, FG_LIST_W, FG_LIST_H),
                                     win, win_reportview, 0, NULL);
      if (list) {
        list->id = FG_LIST_ID;
        send_message(list, RVM_SETVIEWMODE,  RVM_VIEW_LARGE_ICON, NULL);
        send_message(list, RVM_SETCOLUMNWIDTH, FG_ICON_CELL_W, NULL);
        send_message(list, RVM_SETICONSIZE,  FG_ICON_SIZE, NULL);
        send_message(list, RVM_SETICONSTRIP, 0, &st->thumb_strip);
        send_message(list, RVM_SETREDRAW, 0, NULL);
        int count = g_app ? g_app->filter_count : 0;
        for (int i = 0; i < count; i++) {
          reportview_item_t item = {
            .text     = g_app->filters[i].name,
            .icon     = i,
            .color    = 0xffffffff,
          };
          send_message(list, RVM_ADDITEM, 0, &item);
        }
        send_message(list, RVM_SETREDRAW, 1, NULL);
        if (st->selected >= 0)
          send_message(list, RVM_SETSELECTION, (uint32_t)st->selected, NULL);
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
      irect16_t preview = R(FG_LEFT_X, FG_LEFT_Y, FG_MAIN_SIZE, FG_MAIN_SIZE);
      fill_rect(get_sys_color(brWindowDarkBg), rect_inset(preview, -2));
      draw_filter_preview(st, st ? st->selected : -1, preview);
      if (st && st->selected >= 0 && g_app && st->selected < g_app->filter_count) {
        irect16_t name_r = R(FG_LEFT_X, FG_LEFT_Y + FG_MAIN_SIZE + 10,
                          FG_MAIN_SIZE, CONTROL_HEIGHT);
        draw_text_clipped(FONT_SMALL, g_app->filters[st->selected].name, &name_r,
                          get_sys_color(brTextNormal), TEXT_ALIGN_CENTER);
      } else {
        irect16_t msg_r = R(FG_LEFT_X, FG_LEFT_Y + FG_MAIN_SIZE + 10,
                         FG_MAIN_SIZE, CONTROL_HEIGHT);
        draw_text_clipped(FONT_SMALL, "No filters loaded", &msg_r,
                          get_sys_color(brTextDisabled), TEXT_ALIGN_CENTER);
      }
      return false;
    }

    case evCommand:
      if (HIWORD(wparam) == RVN_SELCHANGE) {
        // Selection changed in the thumbnail list — update the large preview.
        window_t *src = (window_t *)lparam;
        if (st && src && src->id == FG_LIST_ID) {
          st->selected = (int)(uint16_t)LOWORD(wparam);
          invalidate_window(win);
        }
        return true;
      }
      if (HIWORD(wparam) == RVN_DBLCLK) {
        // Double-click in the list acts like pressing OK.
        window_t *src = (window_t *)lparam;
        if (st && src && src->id == FG_LIST_ID) {
          st->selected = (int)(uint16_t)LOWORD(wparam);
          filter_gallery_accept(win, st);
        }
        return true;
      }
      if (HIWORD(wparam) == btnClicked) {
        uint16_t id = LOWORD(wparam);
        if (id == FG_BTN_OK) {
          filter_gallery_accept(win, st);
          return true;
        }
        if (id == FG_BTN_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    case evDestroy:
      if (st) {
        if (st->preview_tex) {
          R_DeleteTexture(st->preview_tex);
          st->preview_tex = 0;
        }
        if (st->thumb_sheet_tex) {
          R_DeleteTexture(st->thumb_sheet_tex);
          st->thumb_sheet_tex = 0;
        }
      }
      return false;
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

  irect16_t wr = {0, 0, FG_CLIENT_W, FG_CLIENT_H};
  adjust_window_rect(&wr, WINDOW_DIALOG | WINDOW_NOTRAYBUTTON);
  uint32_t res = show_dialog_ex("Filter Gallery", wr.w, wr.h, parent,
                                WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                filter_gallery_proc, &st);
  if (res && st.accepted && doc->win && st.selected >= 0 && st.selected < g_app->filter_count) {
    send_message(doc->win, evStatusBar, 0, (void *)g_app->filters[st.selected].name);
  }
  return res && st.accepted;
}
