// Onion Skin settings dialog

#include "imageeditor.h"

typedef struct {
  bool    accepted;
  bool    enabled;
  float   prev[ONION_SKIN_MAX_STEPS];
  float   next[ONION_SKIN_MAX_STEPS];
  bool    sel_is_next;
  int     sel_idx;

  bool    original_enabled;
  float   original_prev[ONION_SKIN_MAX_STEPS];
  float   original_next[ONION_SKIN_MAX_STEPS];
} onion_skin_state_t;

static float onion_clamp_percent(float v) {
  return CLAMP(v, 0, 100);
}

static int onion_count_nonzero(const float *vals, int n) {
  int count = 0;
  if (!vals || n <= 0) return 0;
  for (int i = 0; i < n; i++) {
    if (vals[i] > 0)
      count = i + 1;
  }
  return count;
}

static float onion_selected_value(const onion_skin_state_t *st) {
  if (!st) return 0;
  return st->sel_is_next ? st->next[st->sel_idx] : st->prev[st->sel_idx];
}

static void onion_set_selected_value(onion_skin_state_t *st, float value) {
  if (!st) return;
  float v = onion_clamp_percent(value);
  if (st->sel_is_next)
    st->next[st->sel_idx] = v;
  else
    st->prev[st->sel_idx] = v;
}

static void onion_apply_runtime(const onion_skin_state_t *st) {
  if (!g_app || !st) return;
  g_app->anim_trace_enabled = st->enabled;
  memcpy(g_app->anim_trace_prev_opacity, st->prev, sizeof(st->prev));
  memcpy(g_app->anim_trace_next_opacity, st->next, sizeof(st->next));
  g_app->anim_trace_frames = MAX(
      onion_count_nonzero(g_app->anim_trace_prev_opacity, ONION_SKIN_MAX_STEPS),
      onion_count_nonzero(g_app->anim_trace_next_opacity, ONION_SKIN_MAX_STEPS));
  IE_TRACE("onion apply enabled=%d selected=%s:%d opacity=%g span=%d",
           st->enabled, st->sel_is_next ? "next" : "prev", st->sel_idx,
           onion_selected_value(st), g_app->anim_trace_frames);
}

static void onion_restore_runtime(const onion_skin_state_t *st) {
  if (!g_app || !st) return;
  g_app->anim_trace_enabled = st->original_enabled;
  memcpy(g_app->anim_trace_prev_opacity, st->original_prev, sizeof(st->original_prev));
  memcpy(g_app->anim_trace_next_opacity, st->original_next, sizeof(st->original_next));
  g_app->anim_trace_frames = MAX(
      onion_count_nonzero(g_app->anim_trace_prev_opacity, ONION_SKIN_MAX_STEPS),
      onion_count_nonzero(g_app->anim_trace_next_opacity, ONION_SKIN_MAX_STEPS));
  IE_TRACE("onion restore enabled=%d span=%d", st->original_enabled, g_app->anim_trace_frames);
}

static void onion_refresh_preview(void) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;
  if (doc && doc->canvas_win)
    invalidate_window(doc->canvas_win);
  timeline_win_refresh();
}

static void onion_sync_buttons(window_t *win, const onion_skin_state_t *st) {
  static const uint16_t prev_ids[ONION_SKIN_MAX_STEPS] = {
    ID_ONION_SKIN_PREV1, ID_ONION_SKIN_PREV2, ID_ONION_SKIN_PREV3, ID_ONION_SKIN_PREV4
  };
  static const uint16_t next_ids[ONION_SKIN_MAX_STEPS] = {
    ID_ONION_SKIN_NEXT1, ID_ONION_SKIN_NEXT2, ID_ONION_SKIN_NEXT3, ID_ONION_SKIN_NEXT4
  };
  if (!win || !st) return;

  for (int i = 0; i < ONION_SKIN_MAX_STEPS; i++) {
    window_t *btn = get_window_item(win, prev_ids[i]);
    if (btn) {
      set_window_item_text(win, prev_ids[i], "%g%%", st->prev[i]);
      send_message(btn, btnSetCheck,
                   (!st->sel_is_next && st->sel_idx == i) ? btnStateChecked : btnStateUnchecked,
                   NULL);
    }
    btn = get_window_item(win, next_ids[i]);
    if (btn) {
      set_window_item_text(win, next_ids[i], "%g%%", st->next[i]);
      send_message(btn, btnSetCheck,
                   (st->sel_is_next && st->sel_idx == i) ? btnStateChecked : btnStateUnchecked,
                   NULL);
    }
  }
}

static void onion_sync_slider(window_t *win, const onion_skin_state_t *st) {
  if (!win || !st) return;
  float v = onion_selected_value(st);
  window_t *slider = get_window_item(win, ID_ONION_SKIN_VALUE);
  if (slider)
    send_message(slider, slSetPos, 0, (void *)(intptr_t)(v * 2.0f));

  set_window_item_text(win, ID_ONION_SKIN_VALUE_LABEL, "Opacity: %g%%", v);
  set_window_item_text(win, ID_ONION_SKIN_SELECTED_LABEL,
                       "Selected: %s %d (%g%%)",
                       st->sel_is_next ? "Next" : "Previous",
                       st->sel_idx + 1, v);
}

static void onion_sync_enabled(window_t *win, const onion_skin_state_t *st) {
  if (!win || !st) return;
  window_t *chk = get_window_item(win, ID_ONION_SKIN_ENABLED);
  if (chk)
    send_message(chk, btnSetCheck,
                 st->enabled ? btnStateChecked : btnStateUnchecked, NULL);
}

static void onion_sync_ui(window_t *win, const onion_skin_state_t *st) {
  onion_sync_enabled(win, st);
  onion_sync_buttons(win, st);
  onion_sync_slider(win, st);
}

static result_t onion_skin_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  onion_skin_state_t *st = (onion_skin_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      st = (onion_skin_state_t *)lparam;
      if (!st) return false;
      win->userdata = st;

      st->accepted = false;
      st->sel_is_next = false;
      st->sel_idx = 0;
      st->enabled = st->original_enabled;
      memcpy(st->prev, st->original_prev, sizeof(st->prev));
      memcpy(st->next, st->original_next, sizeof(st->next));

      window_t *slider = get_window_item(win, ID_ONION_SKIN_VALUE);
      if (slider) {
        slider_range_t range = {0, 200};
        send_message(slider, slSetRange, 0, &range);
        // Half-percent steps preserve fractional frame opacities.
        send_message(slider, slSetCount, 1, NULL);
      }

      onion_sync_ui(win, st);
      onion_apply_runtime(st);
      onion_refresh_preview();
      return true;
    }

    case evCommand: {
      uint16_t notif = HIWORD(wparam);
      window_t *src = (window_t *)lparam;
      if (!st || !src) return false;
      IE_TRACE("onion command win=%p source=%u notification=%u selected=%s:%d opacity=%g",
               (void *)win, (unsigned)src->id, notif,
               st->sel_is_next ? "next" : "prev", st->sel_idx, onion_selected_value(st));

      if (src->id == ID_ONION_SKIN_VALUE &&
          notif == sliderValueChanged) {
        onion_set_selected_value(st, (float)send_message(src, slGetPos, 0, NULL) / 2.0f);
        onion_sync_ui(win, st);
        onion_apply_runtime(st);
        onion_refresh_preview();
        return true;
      }

      if (notif != btnClicked) return false;

      if (src->id == ID_ONION_SKIN_ENABLED) {
        st->enabled = (send_message(src, btnGetCheck, 0, NULL) == btnStateChecked);
        onion_apply_runtime(st);
        onion_refresh_preview();
        return true;
      }

      if (src->id >= ID_ONION_SKIN_PREV1 && src->id <= ID_ONION_SKIN_PREV4) {
        st->sel_is_next = false;
        st->sel_idx = (int)(src->id - ID_ONION_SKIN_PREV1);
        onion_sync_ui(win, st);
        return true;
      }

      if (src->id >= ID_ONION_SKIN_NEXT1 && src->id <= ID_ONION_SKIN_NEXT4) {
        st->sel_is_next = true;
        st->sel_idx = (int)(src->id - ID_ONION_SKIN_NEXT1);
        onion_sync_ui(win, st);
        return true;
      }

      if (src->id == ID_ONION_SKIN_OK) {
        st->accepted = true;
        end_dialog(win, 1);
        return true;
      }

      if (src->id == ID_ONION_SKIN_CANCEL) {
        end_dialog(win, 0);
        return true;
      }

      return false;
    }

    case evDestroy:
      if (st && !st->accepted) {
        onion_restore_runtime(st);
        onion_refresh_preview();
      }
      return false;

    default:
      return false;
  }
}

bool show_onion_skin_dialog(window_t *parent) {
  if (!g_app) return false;

  onion_skin_state_t st = {0};
  st.original_enabled = g_app->anim_trace_enabled;
  memcpy(st.original_prev, g_app->anim_trace_prev_opacity, sizeof(st.original_prev));
  memcpy(st.original_next, g_app->anim_trace_next_opacity, sizeof(st.original_next));

  show_dialog_from_form_ex(&imageeditor_onion_skin_form, "Onion Skin", parent,
                           WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                           onion_skin_proc, &st);
  return st.accepted;
}
