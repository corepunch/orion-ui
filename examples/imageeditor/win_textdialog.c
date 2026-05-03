#include "imageeditor.h"

#define TD_SIZE_MIN     8
#define TD_SIZE_MAX    72

typedef struct {
  text_options_t *opts;    // caller-provided options (in/out)
  bool            accepted;
} td_state_t;

static void td_sync_size_label(window_t *win, const td_state_t *st) {
  if (!win || !st || !st->opts) return;
  set_window_item_text(win, TD_ID_SIZE_LBL, "Size: %dpx", st->opts->font_size);
}

static void paint_td(window_t *win, const td_state_t *st) {
  if (!win || !st || !st->opts) return;
  window_t *swatch = get_window_item(win, TD_ID_SWATCH);
  if (!swatch) return;
  fill_rect(get_sys_color(brDarkEdge),
            R(swatch->frame.x - 1, swatch->frame.y - 1,
              swatch->frame.w + 2, swatch->frame.h + 2));
  fill_rect(st->opts->color, swatch->frame);
}

static result_t td_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  td_state_t *st = (td_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      st = (td_state_t *)lparam;
      win->userdata = st;

      set_window_item_text(win, TD_ID_EDIT, "%s", st->opts->text);
      window_t *aa = get_window_item(win, TD_ID_AA);
      if (aa) aa->value = st->opts->antialias;
      window_t *size = get_window_item(win, TD_ID_SIZE);
      if (size) {
        slider_range_t r = {TD_SIZE_MIN, TD_SIZE_MAX};
        send_message(size, slSetRange, 0, &r);
        send_message(size, slSetCount, 1, NULL);
        send_message(size, slSetPos, 0, (void *)(intptr_t)st->opts->font_size);
      }
      td_sync_size_label(win, st);
      window_t *ed = get_window_item(win, TD_ID_EDIT);
      if (ed) set_focus(ed);

      return true;
    }

    case evPaint:
      paint_td(win, st);
      return false;  // let child controls paint themselves

    case evCommand: {
      uint16_t notif = HIWORD(wparam);
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == TD_ID_SIZE &&
          notif >= sliderValueChanged && notif <= sliderValueChanged4) {
        st->opts->font_size = (int)send_message(src, slGetPos, 0, NULL);
        td_sync_size_label(win, st);
        return true;
      }

      if (notif != btnClicked) return false;

      if (src->id == TD_ID_OK) {
        // Copy text from the edit box
        window_t *ed = get_window_item(win, TD_ID_EDIT);
        if (ed) {
          strncpy(st->opts->text, ed->title, sizeof(st->opts->text) - 1);
          st->opts->text[sizeof(st->opts->text) - 1] = '\0';
        }
        // Read anti-alias state from checkbox
        window_t *aa = get_window_item(win, TD_ID_AA);
        if (aa) st->opts->antialias = aa->value;
        st->accepted = true;
        end_dialog(win, 1);
        return true;
      }

      if (src->id == TD_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }

      if (src->id == TD_ID_COLOR) {
        // Open color picker to change text color
        uint32_t new_col = st->opts->color;
        if (show_color_picker(win, st->opts->color, &new_col)) {
          st->opts->color = new_col;
          invalidate_window(win);
        }
        return true;
      }

      if (src->id == TD_ID_AA) {
        // Checkbox toggles its own value; re-read it in the OK handler.
        return false;
      }
      return false;
    }

    default:
      return false;
  }
}

bool show_text_dialog(window_t *parent, text_options_t *opts) {
  td_state_t st = {0};
  st.opts     = opts;
  st.accepted = false;

  show_dialog_from_form(&imageeditor_text_tool_form, "Insert Text", parent, td_proc, &st);

  return st.accepted && opts->text[0] != '\0';
}
