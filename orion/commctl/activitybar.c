#include "commctl.h"
#include <orion/user/toolbar.h>
#include <orion/user/theme.h>
#include <orion/user/draw.h>

// A navigation toolbar: the same metrics and radio state as the tool palette.
result_t win_activitybar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      const form_ctrl_def_t *def = lparam;
      const activitybar_params_t *params = def ? def->lparam : NULL;
      send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
      send_message(win, tbSetButtonSize, TB_SPACING, NULL);
      if (params) send_message(win, tbSetItems, params->count, (void *)params->items);
      toolbar_state_t *tb = toolbar_get_state(win);
      for (int i = 0; tb && i < tb->item_count; i++)
        if (tb->items[i].type == TOOLBAR_ITEM_BUTTON) {
          send_message(win, tbCheckButton, tb->items[i].ident, (void *)(intptr_t)1);
          break;
        }
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = lparam;
      isize16_t size = {TB_SPACING + 2 * toolbar_effective_padding(win), 1};
      toolbar_state_t *tb = toolbar_get_state(win);
      if (tb && tb->item_count) send_message(win, tbGetIdealSize, 0, &size);
      if (m) { m->desired_w = size.w; m->desired_h = size.h; }
      return true;
    }
    case evCommand:
      return win->parent ? send_message(win->parent, msg, wparam, HIWORD(wparam) == btnClicked ? win : lparam) : false;
    case evPaint:
      theme_draw(THEME_PART_TOOLBAR, get_client_rect(win), CTRL_NORMAL);
      return true;
    case evDestroy:
      return true; // the window system owns the toolbar's items and host
    default:
      return false;
  }
}
