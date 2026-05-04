// Levels dialog
//
// Shader-backed Levels adjustment for the active layer.  The dialog previews
// the edit live using the renderer's levels shader and bakes it into the layer
// texture when confirmed.

#include "imageeditor.h"
#include "levels_private.h"

typedef struct {
  int in_black;
  int in_mid_value;
  int in_white;
  int out_black;
  int out_white;
  float in_gamma;
} lv_ddx_state_t;

static int lv_mid_from_gamma(const lv_state_t *st);
static void lv_apply_slider_values(lv_state_t *st, uint16_t slider_id);
static void lv_sync_component_views(lv_state_t *st);

static const ctrl_binding_t k_lv_bindings[] = {
  DDX_TEXT(LV_ID_IN_BLACK,   lv_ddx_state_t, in_black),
  DDX_TEXT(LV_ID_IN_GAMMA,   lv_ddx_state_t, in_gamma),
  DDX_TEXT(LV_ID_IN_WHITE,   lv_ddx_state_t, in_white),
  DDX_TEXT(LV_ID_OUT_BLACK,  lv_ddx_state_t, out_black),
  DDX_TEXT(LV_ID_OUT_WHITE,  lv_ddx_state_t, out_white),
};

static int lv_mid_from_gamma(const lv_state_t *st) {
  const float min_mid = 0.10f;
  const float max_mid = 5.00f;
  float t;
  if (!st) return 0;
  if (st->gamma <= 1.0f)
    t = 0.5f * CLAMP((st->gamma - min_mid) / (1.0f - min_mid), 0.0f, 1.0f);
  else
    t = 0.5f + 0.5f * CLAMP((st->gamma - 1.0f) / (max_mid - 1.0f), 0.0f, 1.0f);
  int range = MAX(1, (int)st->white - (int)st->black);
  int pos = (int)lroundf(t * (float)range);
  return CLAMP((int)st->black + pos, 0, 255);
}

static float lv_gamma_from_mid(const lv_ddx_state_t *ddx) {
  const float min_mid = 0.10f;
  const float max_mid = 5.00f;
  float t;
  if (!ddx) return 1.0f;
  int range = MAX(1, ddx->in_white - ddx->in_black);
  t = CLAMP((float)(ddx->in_mid_value - ddx->in_black) / (float)range, 0.0f, 1.0f);
  if (t <= 0.5f)
    return min_mid + (t / 0.5f) * (1.0f - min_mid);
  return 1.0f + ((t - 0.5f) / 0.5f) * (max_mid - 1.0f);
}

static float lv_gamma_from_mid_values(int black, int mid, int white) {
  lv_ddx_state_t ddx;
  memset(&ddx, 0, sizeof(ddx));
  ddx.in_black = black;
  ddx.in_mid_value = mid;
  ddx.in_white = white;
  return lv_gamma_from_mid(&ddx);
}

static void lv_apply_slider_values(lv_state_t *st, uint16_t slider_id) {
  int s0 = 0;
  int s1 = 0;
  int s2 = 0;
  window_t *slider = NULL;
  if (!st) return;

  if (slider_id == LV_ID_IN_SLIDER) {
    slider = st->in_slider_win;
    if (!slider) return;
    s0 = (int)send_message(slider, slGetPos, LV_STRIP_INDEX_MIN, NULL);
    s1 = (int)send_message(slider, slGetPos, LV_STRIP_INDEX_MID, NULL);
    s2 = (int)send_message(slider, slGetPos, LV_STRIP_INDEX_MAX, NULL);
    s0 = CLAMP(s0, 0, 255);
    s2 = CLAMP(s2, 0, 255);
    if (s0 >= s2) s0 = s2 ? s2 - 1 : 0;
    s1 = CLAMP(s1, s0, s2);

    st->black = (uint8_t)s0;
    st->white = (uint8_t)s2;
    st->gamma = CLAMP(lv_gamma_from_mid_values(s0, s1, s2), 0.10f, 5.00f);
    return;
  }

  if (slider_id == LV_ID_OUT_SLIDER) {
    slider = st->out_slider_win;
    if (!slider) return;
    s0 = (int)send_message(slider, slGetPos, LV_STRIP_INDEX_MIN, NULL);
    s1 = (int)send_message(slider, slGetPos, LV_STRIP_INDEX_MID, NULL);
    s0 = CLAMP(s0, 0, 255);
    s1 = CLAMP(s1, 0, 255);
    if (s0 >= s1) s0 = s1 ? s1 - 1 : 0;

    st->out_black = (uint8_t)s0;
    st->out_white = (uint8_t)s1;
  }
}

static void lv_rebuild_histogram(lv_state_t *st) {
  memset(st->hist, 0, sizeof(st->hist));
  st->hist_max = 0;
  if (!st->doc || st->layer_idx < 0 || st->layer_idx >= st->doc->layer_count)
    return;
  const layer_t *lay = st->doc->layers[st->layer_idx];
  if (!lay || !lay->pixels) return;
  size_t n = (size_t)st->doc->canvas_w * st->doc->canvas_h;
  for (size_t i = 0; i < n; i++) {
    const uint8_t *px = lay->pixels + i * 4;
    uint8_t v = (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
    uint32_t c = ++st->hist[v];
    if (c > st->hist_max) st->hist_max = c;
  }
}

static void lv_sync_component_views(lv_state_t *st) {
  lv_graph_data_t graph_data;

  if (!st) return;

  memset(&graph_data, 0, sizeof(graph_data));
  graph_data.black = st->black;
  graph_data.white = st->white;
  graph_data.gamma = st->gamma;
  graph_data.hist_max = st->hist_max;
  memcpy(graph_data.hist, st->hist, sizeof(graph_data.hist));

  if (st->graph_win)
    send_message(st->graph_win, lvGraphSetData, 0, &graph_data);

  if (st->in_slider_win) {
    send_message(st->in_slider_win, slSetPos, LV_STRIP_INDEX_MIN, (void *)(intptr_t)st->black);
    send_message(st->in_slider_win, slSetPos, LV_STRIP_INDEX_MID, (void *)(intptr_t)lv_mid_from_gamma(st));
    send_message(st->in_slider_win, slSetPos, LV_STRIP_INDEX_MAX, (void *)(intptr_t)st->white);
  }

  if (st->out_slider_win) {
    send_message(st->out_slider_win, slSetPos, LV_STRIP_INDEX_MIN, (void *)(intptr_t)st->out_black);
    send_message(st->out_slider_win, slSetPos, LV_STRIP_INDEX_MID, (void *)(intptr_t)st->out_white);
  }
}

static void lv_sync_preview(lv_state_t *st) {
  if (!st || !st->doc) return;
  ui_render_effect_params_t p = {{0}};
  p.f[0] = (float)st->black / 255.0f;
  p.f[1] = (float)st->white / 255.0f;
  p.f[2] = st->gamma;
  p.f[3] = (float)st->out_black / 255.0f;
  p.f[4] = (float)st->out_white / 255.0f;
  layer_set_preview_effect(st->doc, st->layer_idx, UI_RENDER_EFFECT_LEVELS, &p);
  lv_sync_component_views(st);
}

static void lv_set_defaults(lv_state_t *st) {
  st->black = 0;
  st->white = 255;
  st->gamma = 1.0f;
  st->out_black = 0;
  st->out_white = 255;
  lv_sync_preview(st);
}

static void lv_ddx_from_comp(const lv_state_t *st, lv_ddx_state_t *ddx) {
  if (!st || !ddx) return;
  ddx->in_black = st->black;
  ddx->in_white = st->white;
  ddx->in_mid_value = lv_mid_from_gamma(st);
  ddx->out_black = st->out_black;
  ddx->out_white = st->out_white;
  ddx->in_gamma = st->gamma;
}

static void lv_comp_from_ddx(lv_state_t *st, const lv_ddx_state_t *ddx) {
  if (!st) return;
  if (!ddx) return;

  st->black = (uint8_t)CLAMP(ddx->in_black, 0, 255);
  st->white = (uint8_t)CLAMP(ddx->in_white, 0, 255);
  st->out_black = (uint8_t)CLAMP(ddx->out_black, 0, 255);
  st->out_white = (uint8_t)CLAMP(ddx->out_white, 0, 255);

  if (st->black >= st->white)
    st->black = (uint8_t)(st->white ? st->white - 1 : 0);
  if (st->white <= st->black)
    st->white = (uint8_t)(st->black + 1);

  st->gamma = CLAMP(ddx->in_gamma, 0.10f, 5.00f);

  if (st->out_black >= st->out_white)
    st->out_black = (uint8_t)(st->out_white ? st->out_white - 1 : 0);
  if (st->out_white <= st->out_black)
    st->out_white = (uint8_t)(st->out_black + 1);
}

static void lv_sync_edit_fields(lv_state_t *st) {
  lv_ddx_state_t ddx;
  if (!st || !st->dlg_win) return;
  memset(&ddx, 0, sizeof(ddx));
  lv_ddx_from_comp(st, &ddx);
  dialog_push(st->dlg_win, &ddx, k_lv_bindings, ARRAY_LEN(k_lv_bindings));
}

static bool lv_pull_bindings_for_command(lv_state_t *st, uint16_t command) {
  lv_ddx_state_t ddx;
  int pulled;
  if (!st || !st->dlg_win) return false;
  memset(&ddx, 0, sizeof(ddx));
  lv_ddx_from_comp(st, &ddx);
  pulled = dialog_pull_command(st->dlg_win, &ddx, k_lv_bindings,
                               ARRAY_LEN(k_lv_bindings), command);
  if (pulled <= 0) return false;
  lv_comp_from_ddx(st, &ddx);
  return true;
}

static result_t levels_dlg_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  lv_state_t *st = (lv_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      st = (lv_state_t *)lparam;
      win->userdata = st;

      st->dlg_win = win;

      st->graph_win = get_window_item(win, LV_ID_GRAPH);
      st->in_slider_win = get_window_item(win, LV_ID_IN_SLIDER);
      st->out_slider_win = get_window_item(win, LV_ID_OUT_SLIDER);

      /* One-time slider setup: range and handle count are invariant. */
      if (st->in_slider_win) {
        slider_range_t r = {0, 255};
        send_message(st->in_slider_win, slSetRange, 0, &r);
        send_message(st->in_slider_win, slSetCount, 3, NULL);
      }
      if (st->out_slider_win) {
        slider_range_t r = {0, 255};
        send_message(st->out_slider_win, slSetRange, 0, &r);
        send_message(st->out_slider_win, slSetCount, 2, NULL);
      }

      lv_rebuild_histogram(st);
      lv_sync_edit_fields(st);
      lv_sync_preview(st);
      return true;
    }

    case evCommand:
      if (!st) return false;
      {
        window_t *src = (window_t *)lparam;
        uint16_t notif = HIWORD(wparam);
        uint16_t src_id = src ? (uint16_t)src->id : 0;

        if (notif == ddxDataChanged) {
          lv_sync_edit_fields(st);
          lv_sync_preview(st);
          return true;
        }

        if (src && src_id == LV_ID_IN_SLIDER &&
            notif >= sliderValueChanged && notif <= sliderValueChanged4) {
          lv_apply_slider_values(st, LV_ID_IN_SLIDER);
          send_message(win, evCommand,
                       MAKEWPARAM(src_id, ddxDataChanged), st);
          return true;
        }

        if (src && src_id == LV_ID_OUT_SLIDER &&
            notif >= sliderValueChanged && notif <= sliderValueChanged4) {
          lv_apply_slider_values(st, LV_ID_OUT_SLIDER);
          send_message(win, evCommand,
                       MAKEWPARAM(src_id, ddxDataChanged), st);
          return true;
        }

        if (lv_pull_bindings_for_command(st, notif)) {
          send_message(win, evCommand,
                       MAKEWPARAM(src_id, ddxDataChanged), st);
          return true;
        }

        if (!src) return false;
        if (notif != btnClicked) return false;
        if (src->id == LV_ID_OK) {
          st->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == LV_ID_CANCEL) {
          layer_clear_preview_effect(st->doc, st->layer_idx);
          end_dialog(win, 0);
          return true;
        }
        if (src->id == LV_ID_RESET) {
          lv_set_defaults(st);
          lv_sync_edit_fields(st);
          return true;
        }
      }
      return false;

    case evPaint:
      return false;

    case evDestroy:
      if (st && !st->accepted)
        layer_clear_preview_effect(st->doc, st->layer_idx);
      return false;

    default:
      return false;
  }
}

bool show_levels_dialog(window_t *parent) {
  if (!g_app || !g_app->active_doc) return false;
  if (!find_window_class_proc(LV_GRAPH_CLASS_NAME)) {
    IE_DEBUG("Levels dialog unavailable: imageeditor_components plugin is not loaded");
    if (parent)
      message_box(parent,
                  "Levels controls are unavailable because the image editor components plugin did not load.",
                  "Levels",
                  MB_OK);
    return false;
  }

  canvas_doc_t *doc = g_app->active_doc;
  if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return false;

  lv_state_t st;
  memset(&st, 0, sizeof(st));
  st.doc = doc;
  st.layer_idx = doc->active_layer;
  st.gamma = 1.0f;
  st.black = 0;
  st.white = 255;
  st.out_black = 0;
  st.out_white = 255;
  lv_rebuild_histogram(&st);

  uint32_t res = show_dialog_from_form_ex(&imageeditor_levels_form, "Levels", parent,
                                          WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                          levels_dlg_proc, &st);
  if (!res || !st.accepted) return false;
  if (!layer_commit_preview_effect(doc, st.layer_idx)) {
    layer_clear_preview_effect(doc, st.layer_idx);
    return false;
  }
  return true;
}
