// Filter gallery dialog: one-shot browser for the Photo menu shader presets.
// The thumbnail grid is rendered by win_reportview in RVM_VIEW_LARGE_ICON mode.
// Each thumbnail is baked once at dialog-open time and stored in a single GPU
// texture strip so no shader runs per frame during display.

#include "imageeditor.h"

#if UI_WINDOW_SCALE > 1
#define FG_ICON_SIZE       64
#define FG_ICON_CELL_W     74
#else
#define FG_ICON_SIZE       64
#define FG_ICON_CELL_W     72
#endif
#define FG_PREVIEW_TEX    384

typedef struct {
  canvas_doc_t *doc;
  int selected;
  uint32_t preview_tex;
  uint32_t thumb_sheet_tex;   // GL texture owning the thumbnail strip pixels
  bitmap_strip_t thumb_strip; // strip descriptor pointing at thumb_sheet_tex
  bool accepted;
} filter_gallery_state_t;

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

static void filter_gallery_sync_preview(window_t *win, filter_gallery_state_t *st) {
  if (!win) return;
  window_t *preview = get_window_item(win, FG_ID_PREVIEW);
  window_t *label = get_window_item(win, FG_ID_LABEL);
  if (label) {
    const char *text = "No filters loaded";
    if (st && st->selected >= 0 && g_app && st->selected < g_app->filter_count)
      text = g_app->filters[st->selected].name;
    snprintf(label->title, sizeof(label->title), "%s", text);
    invalidate_window(label);
  }
  if (preview) {
    fg_preview_data_t data = {
      .texture = st ? st->preview_tex : 0,
      .program = (st && st->selected >= 0 && g_app &&
                  st->selected < g_app->filter_count)
                 ? g_app->filters[st->selected].program : 0,
    };
    send_message(preview, fgPreviewSetData, 0, &data);
  }
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
      filter_gallery_sync_preview(win, st);

      window_t *list = get_window_item(win, FG_ID_LIST);
      if (list) {
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
      return true;
    }

    case evPaint:
      return false;

    case evCommand:
      if (HIWORD(wparam) == RVN_SELCHANGE) {
        // Selection changed in the thumbnail list — update the large preview.
        window_t *src = (window_t *)lparam;
        if (st && src && src->id == FG_ID_LIST) {
          st->selected = (int)(uint16_t)LOWORD(wparam);
          filter_gallery_sync_preview(win, st);
        }
        return true;
      }
      if (HIWORD(wparam) == RVN_DBLCLK) {
        // Double-click in the list acts like pressing OK.
        window_t *src = (window_t *)lparam;
        if (st && src && src->id == FG_ID_LIST) {
          st->selected = (int)(uint16_t)LOWORD(wparam);
          filter_gallery_accept(win, st);
        }
        return true;
      }
      if (HIWORD(wparam) == btnClicked) {
        uint16_t id = LOWORD(wparam);
        if (id == FG_ID_OK) {
          filter_gallery_accept(win, st);
          return true;
        }
        if (id == FG_ID_CANCEL) {
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
  if (!find_window_class_proc(FG_PREVIEW_CLASS_NAME)) {
    IE_DEBUG("Filter Gallery unavailable: imageeditor_components plugin is not loaded");
    if (parent)
      message_box(parent,
                  "Filter Gallery controls are unavailable because the image editor components plugin did not load.",
                  "Filter Gallery",
                  MB_OK);
    return false;
  }
  canvas_doc_t *doc = g_app->active_doc;
  if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count)
    return false;

  filter_gallery_state_t st;
  memset(&st, 0, sizeof(st));
  st.doc = doc;
  st.selected = -1;

  uint32_t res = show_dialog_from_form_ex(&imageeditor_filter_gallery_form,
                                "Filter Gallery", parent,
                                WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                filter_gallery_proc, &st);
  if (res && st.accepted && doc->win && st.selected >= 0 && st.selected < g_app->filter_count) {
    send_message(doc->win, evStatusBar, 0, (void *)g_app->filters[st.selected].name);
  }
  return res && st.accepted;
}
