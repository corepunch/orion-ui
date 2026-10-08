#include "browser.h"

void browser_set_body_text(window_t *win, const char *text) {
  window_t *body = get_window_item(win, ID_BODY_VIEW);
  if (!body) return;
  send_message(body, edSetText, 0, (void *)(text ? text : ""));
}

void browser_sync_nav_buttons(window_t *win) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  if (!st) return;
  window_t *back = get_window_item(win, ID_TB_BACK);
  window_t *fwd = get_window_item(win, ID_TB_FWD);
  if (back) enable_window(back, st->history_index > 0);
  if (fwd) enable_window(fwd, st->history_index >= 0 && st->history_index + 1 < st->history_count);
}

void browser_rebuild_toolbar(window_t *win) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  const int btn_w = TB_SPACING;
  const int addr_w = MAX(120, win->frame.w - (btn_w * 4) - 24);
  const toolbar_item_t items[] = {
    { TOOLBAR_ITEM_BUTTON, ID_TB_BACK, "arrow-left", btn_w, 0, NULL, "Go back" },
    { TOOLBAR_ITEM_BUTTON, ID_TB_FWD, "arrow-right", btn_w, 0, NULL, "Go forward" },
    { TOOLBAR_ITEM_BUTTON, ID_TB_HOME, NULL, btn_w, 0, NULL, "Go home" },
    { TOOLBAR_ITEM_BUTTON, ID_TB_REFRESH, "refresh", btn_w, 0, NULL, "Refresh" },
    { TOOLBAR_ITEM_SPACER, 0, NULL, 4, 0, NULL, NULL },
    { TOOLBAR_ITEM_TEXTEDIT, ID_TB_ADDR, NULL, addr_w, 0,
      (st && st->current_url[0]) ? st->current_url : "https://example.com", NULL },
  };
  send_message(win, tbSetItems, (uint32_t)(sizeof(items) / sizeof(items[0])), (void *)items);
  browser_sync_nav_buttons(win);
}

void browser_update_layout(window_t *win) {
  irect16_t cr = get_client_rect(win);
  window_t *body = get_window_item(win, ID_BODY_VIEW);
  if (body) {
    move_window(body, cr.x, cr.y);
    resize_window(body, cr.w, cr.h);
  }
}
