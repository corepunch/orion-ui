// Blur dialog
//
// Lets the user choose the blur radius in pixels before applying the built-in
// blur filter to the active layer.

#include "imageeditor.h"

#define BLUR_MIN_RADIUS  1
#define BLUR_MAX_RADIUS 16
#define BLUR_DEFAULT_RADIUS 4

typedef struct {
  canvas_doc_t *doc;
  int layer_idx;
  int amount;
  bool preview_enabled;
  bool accepted;
  window_t *dlg_win;
  window_t *preview_win;
  window_t *amount_slider;
} bl_state_t;

static void bl_sync_amount_label(window_t *win, const bl_state_t *st) {
  if (!win || !st) return;
  set_window_item_text(win, BL_ID_AMOUNT_LBL, "Blur: %dpx", st->amount);
}

static void bl_apply_preview(bl_state_t *st) {
  ui_render_effect_params_t p = {{0}};
  if (!st || !st->doc) return;
  p.f[0] = 1.0f / (float)MAX(1, st->doc->canvas_w);
  p.f[1] = 0.0f;
  p.f[2] = (float)st->amount;
  layer_set_preview_effect(st->doc, st->layer_idx, UI_RENDER_EFFECT_BLUR, &p);
}

static void bl_sync_preview(bl_state_t *st) {
  if (!st || !st->doc) return;
  if (st->preview_enabled)
    bl_apply_preview(st);
  else
    layer_clear_preview_effect(st->doc, st->layer_idx);
}

static result_t blur_dlg_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  bl_state_t *st = (bl_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      st = (bl_state_t *)lparam;
      win->userdata = st;
      st->dlg_win = win;

      st->preview_win = get_window_item(win, BL_ID_PREVIEW);
      st->amount_slider = get_window_item(win, BL_ID_AMOUNT);
      st->preview_enabled = true;
      if (st->preview_win)
        send_message(st->preview_win, btnSetCheck, btnStateChecked, NULL);

      if (st->amount_slider) {
        slider_range_t r = {BLUR_MIN_RADIUS, BLUR_MAX_RADIUS};
        st->amount = CLAMP(st->amount, BLUR_MIN_RADIUS, BLUR_MAX_RADIUS);
        send_message(st->amount_slider, slSetRange, 0, &r);
        send_message(st->amount_slider, slSetCount, 1, NULL);
        send_message(st->amount_slider, slSetPos, 0, (void *)(intptr_t)st->amount);
      }
      bl_sync_amount_label(win, st);
      bl_sync_preview(st);
      if (st->amount_slider) set_focus(st->amount_slider);
      return true;
    }

    case evCommand: {
      uint16_t notif = HIWORD(wparam);
      window_t *src = (window_t *)lparam;
      if (!st || !src) return false;

      if (src->id == BL_ID_AMOUNT &&
          notif >= sliderValueChanged && notif <= sliderValueChanged4) {
        st->amount = (int)send_message(src, slGetPos, 0, NULL);
        st->amount = CLAMP(st->amount, BLUR_MIN_RADIUS, BLUR_MAX_RADIUS);
        bl_sync_amount_label(win, st);
        if (st->preview_enabled)
          bl_apply_preview(st);
        return true;
      }

      if (notif != btnClicked) return false;

      if (src->id == BL_ID_PREVIEW) {
        st->preview_enabled = (send_message(src, btnGetCheck, 0, NULL) == btnStateChecked);
        bl_sync_preview(st);
        return true;
      }

      if (src->id == BL_ID_OK) {
        if (st->preview_enabled)
          layer_clear_preview_effect(st->doc, st->layer_idx);
        st->accepted = true;
        end_dialog(win, 1);
        return true;
      }

      if (src->id == BL_ID_CANCEL) {
        layer_clear_preview_effect(st->doc, st->layer_idx);
        end_dialog(win, 0);
        return true;
      }

      return false;
    }

    case evDestroy:
      if (st && !st->accepted)
        layer_clear_preview_effect(st->doc, st->layer_idx);
      return false;

    default:
      return false;
  }
}

bool show_blur_dialog(window_t *parent, int *out_amount) {
  bl_state_t st = {0};
  canvas_doc_t *doc;

  if (!out_amount) return false;
  if (!g_app || !g_app->active_doc) return false;
  doc = g_app->active_doc;
  if (doc->layer.active < 0 || doc->layer.active >= doc->layer.count)
    return false;

  st.doc = doc;
  st.layer_idx = doc->layer.active;
  st.amount = *out_amount > 0 ? *out_amount : BLUR_DEFAULT_RADIUS;
  st.preview_enabled = true;
  st.accepted = false;

  show_dialog_from_form_ex(&imageeditor_blur_dialog_form, "Blur", parent,
                           WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                           blur_dlg_proc, &st);

  if (!st.accepted) return false;
  layer_clear_preview_effect(doc, st.layer_idx);
  *out_amount = st.amount;
  return true;
}
