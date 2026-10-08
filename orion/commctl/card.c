// Card and Badge controls.
//
// A Card is a container view with a themed face (draw_card) that lays its children out as a vertical
// stack, like NSBox / a UICollectionViewCell's contentView. Put Labels, Badges and nested stacks or
// flow views inside it and it sizes itself from their measure pass, so a tile grid can wrap and
// align any content. A Badge is a self-measuring tinted label.

#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "commctl.h"
#include "layout_shared.h"

// ── Badge ─────────────────────────────────────────────────────────────────────

result_t win_badge(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      uint32_t role = brTextSecondary;
      if (lparam && (uintptr_t)lparam > 0x1000) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        if (cd->color_set && cd->color < brCount) role = cd->color;
      }
      win->userdata = (void *)(uintptr_t)role;
      win->flags |= WINDOW_NOTABSTOP;
      win->layout.h_align = win->layout.v_align = LAYOUT_ALIGN_START;   // keep the intrinsic size
      return true;
    }
    case bdSetColor:
      if (wparam >= brCount) {
        fprintf(stderr, "[badge] win=%u bdSetColor role=%u out of range\n", (unsigned)win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      win->userdata = (void *)(uintptr_t)wparam;
      invalidate_window(win);
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      m->desired_w = measure_badge(FONT_SMALL, win->title);
      m->desired_h = BADGE_HEIGHT;
      return true;
    }
    case evPaint:
      draw_badge(FONT_SMALL, win->title, 0, 0, win->frame.h, get_sys_color((sys_color_idx_t)(uintptr_t)win->userdata));
      return true;
    default:
      return false;
  }
}

// ── Card ──────────────────────────────────────────────────────────────────────

typedef struct {
  uint32_t edge_color;   // packed colour of the accent edge; alpha 0 = none
  uint32_t state;        // CTRL_SELECTED | CTRL_FOCUSED, owned by the containing collection
  bool     hover;
} card_state_t;

static void card_notify(window_t *win, uint16_t code) {
  if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, code), win);
}

result_t win_card(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  card_state_t *card = (card_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      card = calloc(1, sizeof(*card));
      if (!card) {
        fprintf(stderr, "[card] win=%u state allocation failed\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      win_stack(win, msg, wparam, lparam);
      win->userdata = card;
      const theme_t *theme = get_theme();
      int inset = theme->card_ring_width;
      win->layout.layout_padding = (irect16_t){
        inset + theme->card_edge_width + theme->card_padding_x, inset + theme->card_padding_y,
        inset + theme->card_padding_x, inset + theme->card_padding_y };
      win->layout.layout_spacing = 3;
      return true;
    }
    case evDestroy:
      free(card);
      win->userdata = NULL;
      return false;
    case cdSetEdgeColor:
      if (card) { card->edge_color = (uint32_t)(uintptr_t)lparam; invalidate_window(win); }
      return true;
    case cdSetState:
      if (card && card->state != (wparam & (CTRL_SELECTED | CTRL_FOCUSED))) {
        card->state = wparam & (CTRL_SELECTED | CTRL_FOCUSED);
        invalidate_window(win);
      }
      return true;
    case evPaint:
      if (card) {
        ctrl_state_t state = card->state | (card->hover ? CTRL_HOVER : 0);
        if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
        if (window_has_state(win, WINDOW_STATE_PRESSED)) state |= CTRL_PRESSED;
        draw_card(get_client_rect(win), state, card->edge_color);
      }
      layout_paint_children(win);
      return true;
    case evMouseMove:
      if (card && !card->hover) { card->hover = true; invalidate_window(win); }
      track_mouse(win);
      return false;
    case evMouseLeave:
      if (card && card->hover) { card->hover = false; invalidate_window(win); }
      return false;
    case evLeftButtonDown:
      card_notify(win, cdnClicked);
      return true;
    case evLeftButtonDoubleClick:
      card_notify(win, cdnActivated);
      return true;
    case evGetTooltipText:
      if (!lparam || !win->title[0]) return false;
      strncpy((char *)lparam, win->title, 255);
      ((char *)lparam)[255] = '\0';
      return true;
    default:
      return win_stack(win, msg, wparam, lparam);
  }
}
