// Card and Badge controls.
//
// A Card is a container view whose box comes from the style sheet that lays its children out as a vertical
// stack, like NSBox / a UICollectionViewCell's contentView. Put Labels, Badges and nested stacks or
// flow views inside it and it sizes itself from their measure pass, so a tile grid can wrap and
// align any content. A Badge is a self-measuring tinted label.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/svg_icon_loader.h>
#include <orion/user/rect.h>
#include "commctl_internal.h"

// ── Badge ─────────────────────────────────────────────────────────────────────

// A status pill (draw_status_badge): the Badge box of the style sheet, toned by a
// theme role from color= or bdSetColor (the window's --tone, var(--text-success)
// and so on, so it follows the palette), and an optional leading icon from icon=
// or btnSetIconName, so the state never relies on colour alpha alone.
typedef struct {
  char icon[64];
} badge_t;

static void badge_set_tone(window_t *win, uint32_t role) {
  const char *name = theme_sys_color_name((int)role);
  char tone[48];
  snprintf(tone, sizeof(tone), "var(--%s)", name ? name : "text-secondary");
  window_set_style_var(win, "--tone", tone);
}

result_t win_badge(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  badge_t *b = (badge_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      b = calloc(1, sizeof(*b));
      if (!b) {
        fprintf(stderr, "[badge] win=%u state allocation failed\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      uint32_t role = brTextSecondary;
      if (lparam && (uintptr_t)lparam > 0x1000) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        if (cd->color_set && cd->color < brCount) role = cd->color;
      }
      badge_set_tone(win, role);
      win->userdata = b;
      win->flags |= WINDOW_NOTABSTOP;
      win->layout.h_align = win->layout.v_align = LAYOUT_ALIGN_START;   // keep the intrinsic size
      return true;
    }
    case evDestroy:
      free(b);
      win->userdata = NULL;
      return true;
    case bdSetColor:
      if (wparam >= brCount || !b) {
        fprintf(stderr, "[badge] win=%u bdSetColor role=%u out of range\n", (unsigned)win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      badge_set_tone(win, wparam);
      return true;
    case btnSetIconName:
      if (!b) return false;
      snprintf(b->icon, sizeof(b->icon), "%s", lparam ? (const char *)lparam : "");
      if (win->parent) window_layout_sync(win->parent);
      invalidate_window(win);
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      m->desired_w = measure_status_badge(win->title, b ? b->icon : NULL);
      m->desired_h = BADGE_HEIGHT;
      return true;
    }
    case evPaint:
      if (b) draw_status_badge(win, R(0, 0, win->frame.w, win->frame.h), win->title, b->icon, NULL);
      return true;
    default:
      return false;
  }
}

// ── Card ──────────────────────────────────────────────────────────────────────

// The Card box (margin, border-left accent, ring, hover) is the sheet's; the accent colour is the
// window's --tone variable (cdSetEdgeColor).
typedef struct {
  uint32_t state;        // CTRL_SELECTED | CTRL_FOCUSED, owned by the containing collection
  bool     hover;
} card_state_t;

static void card_notify(window_t *win, uint16_t code) {
  if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, code), win);
}

// The content inset: what the box takes for its margin and border, plus the card's padding.
static void card_apply_style(window_t *win) {
  const char *card = ui_selector(win, "Card");
  irect16_t box = theme_content_insets_in(win, THEME_PART_CARD, CTRL_NORMAL);
  int left = ui_px(card, "padding-left"), top = ui_px(card, "padding-top");
  win->layout.layout_padding = (irect16_t){ box.x + left, box.y + top, box.w + left, box.h + top };
  win->ink_overflow = theme_ink_overflow_in(win, THEME_PART_CARD, CTRL_NORMAL);
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
      card_apply_style(win);
      win->layout.layout_spacing = 3;
      return true;
    }
    case evThemeChanged:   // a new theme, or new classes or #id
      card_apply_style(win);
      return false;
    case evDestroy:
      free(card);
      win->userdata = NULL;
      return false;
    case cdSetEdgeColor: {
      uint32_t color = (uint32_t)(uintptr_t)lparam;
      if (color >> 24) window_set_style_var_color(win, "--tone", color);
      else window_set_style_var(win, "--tone", NULL);   // no accent: the sheet's var(--tone, transparent)
      return true;
    }
    case cdSetState:
      if (card && card->state != (wparam & (CTRL_SELECTED | CTRL_FOCUSED))) {
        card->state = wparam & (CTRL_SELECTED | CTRL_FOCUSED);
        invalidate_window(win);
      }
      return true;
    case evPaint:
      if (card) {
        ctrl_state_t state = card->state | (card->hover ? CTRL_HOVER : 0);
        if (window_is_disabled(win)) state |= CTRL_DISABLED;
        if (window_has_state(win, WINDOW_STATE_PRESSED)) state |= CTRL_PRESSED;
        theme_draw_in(win, THEME_PART_CARD, get_client_rect(win), state);
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
    case evParentNotify: {
      const parent_notify_t *pn = lparam;
      if (!pn || !pn->child) {
        fprintf(stderr, "[card] win=%u parent notification missing child\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      window_t *child = pn->child;
      bool passive = child->proc == win_label || child->proc == win_badge || child->proc == win_space ||
                     (child->flags & WINDOW_LAYOUT_CONTAINER);
      if (passive && (pn->child_msg == evLeftButtonDown || pn->child_msg == evLeftButtonDoubleClick)) {
        card_notify(win, pn->child_msg == evLeftButtonDown ? cdnClicked : cdnActivated);
        return true;
      }
      return false;
    }
    case evGetTooltipText:
      if (!lparam || !win->title[0]) return false;
      strncpy((char *)lparam, win->title, 255);
      ((char *)lparam)[255] = '\0';
      return true;
    default:
      return win_stack(win, msg, wparam, lparam);
  }
}
