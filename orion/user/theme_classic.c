// Classic theme — preserves pre-2024 bevel-oriented control appearance.
// Extracted verbatim from draw_impl.c so the visual baseline is unchanged.

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>

// ── Primitive: bevel ──────────────────────────────────────────────────────────

static void classic_draw_bevel(irect16_t r) {
  fill_rect(get_sys_color(brLightEdge), R(r.x-1, r.y-1, r.w+2, 1));
  fill_rect(get_sys_color(brLightEdge), R(r.x-1, r.y-1, 1, r.h+2));
  fill_rect(get_sys_color(brDarkEdge),  R(r.x+r.w, r.y, 1, r.h+1));
  fill_rect(get_sys_color(brDarkEdge),  R(r.x, r.y+r.h, r.w+1, 1));
  fill_rect(get_sys_color(brFlare),     R(r.x-1, r.y-1, 1, 1));
}

// ── Buttons ───────────────────────────────────────────────────────────────────

static void classic_draw_button_bg(irect16_t r, ctrl_state_t state) {
  bool pressed = (state & (CTRL_PRESSED | CTRL_SELECTED)) != 0;
  // Disabled: etched (dimmer inner fill); hover: slightly brighter fill.
  uint32_t inner = (state & CTRL_DISABLED) ? get_sys_color(brWindowDarkBg) :
                   (state & CTRL_HOVER)    ? get_sys_color(brButtonHover)  :
                                             get_sys_color(brControlBg);
  if (pressed) {
    fill_rect(get_sys_color(brDarkEdge),      r);
    fill_rect(get_sys_color(brLightEdge),     R(r.x+1, r.y+1, r.w-1, r.h-1));
    fill_rect(get_sys_color(brDarkEdge),      R(r.x+1, r.y+1, r.w-2, r.h-2));
    fill_rect(get_sys_color(brWindowDarkBg),  R(r.x+2, r.y+2, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare),         R(r.x+r.w-1, r.y+r.h-1, 1, 1));
  } else {
    fill_rect(get_sys_color(brDarkEdge),      r);
    fill_rect(get_sys_color(brLightEdge),     R(r.x, r.y, r.w-1, r.h-1));
    fill_rect(get_sys_color(brDarkEdge),      R(r.x+1, r.y+1, r.w-2, r.h-2));
    fill_rect(inner,                          R(r.x+1, r.y+1, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare),         R(r.x, r.y, 1, 1));
  }
}

// Toolbar item background.  Classic preserves historical borderless appearance
// for ordinary labeled buttons; dropdown parts always draw a bevel.
static void classic_draw_toolbar_item_bg(irect16_t r, ctrl_state_t state,
                                         theme_part_t part) {
  if (part == THEME_PART_TOOLBAR_LABELED_BUTTON) return;
  classic_draw_button_bg(r, state);
}

// ── Toolbar separator ─────────────────────────────────────────────────────────

static void classic_draw_toolbar_separator(irect16_t r) {
  if (r.w > r.h) {
    int my = r.y + r.h / 2;
    fill_rect(get_sys_color(brDarkEdge),  R(r.x + 2, my,     r.w - 4, 1));
    fill_rect(get_sys_color(brLightEdge), R(r.x + 2, my + 1, r.w - 4, 1));
    return;
  }
  int mx = r.x + r.w / 2;
  fill_rect(get_sys_color(brDarkEdge),  R(mx,     r.y + 2, 1, r.h - 4));
  fill_rect(get_sys_color(brLightEdge), R(mx + 1, r.y + 2, 1, r.h - 4));
}

// ── Panel ─────────────────────────────────────────────────────────────────────

static void classic_draw_panel_bg(irect16_t r) {
  fill_rect(get_sys_color(brControlBg), r);
  classic_draw_bevel(r);

}

// ── Titlebar ─────────────────────────────────────────────────────────────────

static void classic_draw_titlebar_bg(irect16_t r, bool focused) {
  fill_rect(get_sys_color(focused ? brAccent : brInactiveTitlebar), r);
}

// ── Statusbar ────────────────────────────────────────────────────────────────

static void classic_draw_statusbar_bg(irect16_t r) {
  fill_rect(get_sys_color(brStatusbarBg), r);
}

// ── Checkbox ─────────────────────────────────────────────────────────────────

static void classic_draw_checkbox_box(irect16_t r, bool checked, ctrl_state_t state) {
  bool focused = (state & CTRL_FOCUSED) != 0;
  bool pressed = (state & CTRL_PRESSED) != 0;

  // Focus background: solid accent fill when focused, nothing when unfocused
  // (the surrounding panel or row background shows through).
  if (focused) {
    irect16_t focus_bg = rect_inset(r, -CHECKBOX_FOCUS_PAD);
    fill_rect(get_sys_color(brAccent), focus_bg);
  }

  // Box bevel — identical to classic button bevel so checkboxes track button style.
  uint32_t box_inner = (state & CTRL_DISABLED) ? get_sys_color(brWindowDarkBg) :
                       (state & CTRL_HOVER)    ? get_sys_color(brButtonHover)  :
                                                 get_sys_color(brControlBg);
  if (pressed) {
    fill_rect(get_sys_color(brDarkEdge),     r);
    fill_rect(get_sys_color(brLightEdge),    R(r.x+1, r.y+1, r.w-1, r.h-1));
    fill_rect(get_sys_color(brDarkEdge),     R(r.x+1, r.y+1, r.w-2, r.h-2));
    fill_rect(get_sys_color(brWindowDarkBg), R(r.x+2, r.y+2, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare),        R(r.x+r.w-1, r.y+r.h-1, 1, 1));
  } else {
    fill_rect(get_sys_color(brDarkEdge),     r);
    fill_rect(get_sys_color(brLightEdge),    R(r.x, r.y, r.w-1, r.h-1));
    fill_rect(get_sys_color(brDarkEdge),     R(r.x+1, r.y+1, r.w-2, r.h-2));
    fill_rect(box_inner,                     R(r.x+1, r.y+1, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare),        R(r.x, r.y, 1, 1));
  }

  if (checked)
    draw_theme_icon_in_rect(THEME_ICON_CHECKMARK, r, get_sys_color(brTextNormal));
}

// ── Combobox ─────────────────────────────────────────────────────────────────

// Combobox uses the same bevel as a button — identical to current direct-draw output.
static void classic_draw_combobox_bg(irect16_t r, ctrl_state_t state) {
  classic_draw_button_bg(r, state);
}

// ── List item ────────────────────────────────────────────────────────────────

static void classic_draw_list_item_bg(irect16_t r, ctrl_state_t state) {
  if (state & CTRL_SELECTED)
    fill_rect(get_sys_color(brTextNormal), r);
}

// ── Slider thumb ─────────────────────────────────────────────────────────────

static void classic_draw_slider_thumb(irect16_t r, bool active) {
  fill_rect(active ? get_sys_color(brAccent) : get_sys_color(brDarkEdge), r);
  fill_rect(get_sys_color(brTextNormal), R(r.x+1, r.y+1, r.w-2, r.h-2));
}

// ── Menu item background ─────────────────────────────────────────────────────

static void classic_draw_menu_item_bg(irect16_t r, ctrl_state_t state) {
  if (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED))
    fill_rect(get_sys_color(brAccent), r);
}

// ── Palette ───────────────────────────────────────────────────────────────────

static void classic_apply_palette(void) {
  g_sys_colors[brTransparent]          = 0x00000000;
  g_sys_colors[brControlBg]            = 0xff3c3c3c;
  g_sys_colors[brWindowDarkBg]         = 0xff2c2c2c;
  g_sys_colors[brWorkspaceBg]          = 0xff1e1e1e;
  g_sys_colors[brActiveTitlebar]       = 0xffD77800;
  g_sys_colors[brActiveTitlebarText]   = 0xffffffff;
  g_sys_colors[brInactiveTitlebar]     = 0xff2c2c2c;
  g_sys_colors[brInactiveTitlebarText] = 0xff787878;
  g_sys_colors[brStatusbarBg]          = 0xff2c2c2c;
  g_sys_colors[brLightEdge]            = 0xff7f7f7f;
  g_sys_colors[brDarkEdge]             = 0xff1a1a1a;
  g_sys_colors[brFlare]                = 0xffcfcfcf;
  g_sys_colors[brAccent]               = 0xffD77800;
  g_sys_colors[brButtonInner]          = 0xff505050;
  g_sys_colors[brButtonHover]          = 0xff5a5a5a;
  g_sys_colors[brTextNormal]           = 0xffc0c0c0;
  g_sys_colors[brTextDisabled]         = 0xff808080;
  g_sys_colors[brTextError]            = 0xffff4444;
  g_sys_colors[brTextSuccess]          = 0xff44ff44;
  g_sys_colors[brBorderFocus]          = 0xff101010;
  g_sys_colors[brBorderActive]         = 0xff808080;
  g_sys_colors[brFolderText]           = 0xffa0d000;
  g_sys_colors[brColumnViewBg]         = 0xff544e47;
  g_sys_colors[brModalOverlay]         = 0x40402000;
  g_sys_colors[brToolbarForeground]    = 0xffd8d8d8;
}

// ── Singleton ────────────────────────────────────────────────────────────────

static void classic_draw_window_chrome(irect16_t titlebar, irect16_t caption,
                                       const char *title, ctrl_state_t state, bool maximizable) {
  bool focused = (state & CTRL_FOCUSED) != 0;
  classic_draw_titlebar_bg(titlebar, focused);
  irect16_t close = rect_split_right(caption, caption.h);
  draw_theme_icon_in_rect(THEME_ICON_CLOSE, close, get_sys_color(brTextNormal));
  caption = rect_trim_right(caption, close.w);
  if (maximizable) {
    draw_theme_icon_in_rect(THEME_ICON_MAXIMIZE, rect_split_right(caption, caption.h),
                            get_sys_color(brTextNormal));
    caption = rect_trim_right(caption, caption.h);
  }
  draw_text_small_clipped(title, &caption,
      get_sys_color(focused ? brActiveTitlebarText : brInactiveTitlebarText), TEXT_PADDING_LEFT);
}

static void classic_draw_statusbar(irect16_t r, const char *text) {
  classic_draw_statusbar_bg(r);
  if (text) draw_text_clipped(FONT_SMALL, text, &r, get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
}

static void classic_draw_part(theme_part_t part, irect16_t r, ctrl_state_t state) {
  bool disabled = (state & CTRL_DISABLED) != 0;
  uint32_t foreground = get_sys_color(disabled ? brTextDisabled : brTextNormal);
  switch (part) {
    case THEME_PART_BUTTON:
      fill_rect((state & CTRL_FOCUSED) ? get_sys_color(brAccent) :
                (state & CTRL_DEFAULT) ? 0xff000000 : get_sys_color(brControlBg), rect_inset(r, -1));
      classic_draw_button_bg(r, state);
      break;
    case THEME_PART_CHECKBOX:            classic_draw_checkbox_box(r, state & CTRL_SELECTED, state); break;
    case THEME_PART_COMBOBOX:            classic_draw_combobox_bg(r, state); break;
    case THEME_PART_COMBOBOX_ARROW:      draw_theme_icon_in_rect(THEME_ICON_ARROW_UPDOWN, r, foreground); break;
    case THEME_PART_TOOLBAR_BUTTON:
    case THEME_PART_TOOLBAR_LABELED_BUTTON:
    case THEME_PART_TOOLBAR_SPLIT_BUTTON:
    case THEME_PART_TOOLBAR_SPLIT_ARROW:
      classic_draw_toolbar_item_bg(r, state, part);
      if (part == THEME_PART_TOOLBAR_SPLIT_ARROW) {
        int cx = r.x + r.w / 2;
        int cy = r.y + r.h / 2 - 1 + ((state & CTRL_PRESSED) ? 1 : 0);
        uint32_t col = get_sys_color(disabled ? brTextDisabled : brToolbarForeground);
        for (int i = 0; i < 4; i++) fill_rect(col, R(cx - 3 + i, cy + i, 7 - 2 * i, 1));
      }
      break;
    case THEME_PART_TOOLBAR_SEPARATOR:   classic_draw_toolbar_separator(r); break;
    case THEME_PART_PANEL:               classic_draw_panel_bg(r); break;
    case THEME_PART_TITLEBAR:            classic_draw_titlebar_bg(r, state & CTRL_FOCUSED); break;
    case THEME_PART_WINDOW_CLOSE:        draw_theme_icon_in_rect(THEME_ICON_CLOSE, r, foreground); break;
    case THEME_PART_STATUSBAR:           classic_draw_statusbar_bg(r); break;
    case THEME_PART_LIST_ITEM:           classic_draw_list_item_bg(r, state); break;
    case THEME_PART_SLIDER_THUMB:        classic_draw_slider_thumb(r, state & CTRL_PRESSED); break;
    case THEME_PART_MENU_ITEM:           if (!disabled) classic_draw_menu_item_bg(r, state); break;
    case THEME_PART_SURFACE:             fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_FIELD:
      fill_rect(get_sys_color((state & CTRL_FOCUSED) ? brAccent : brControlBg), rect_inset(r, -1));
      classic_draw_button_bg(r, state | CTRL_PRESSED);
      break;
    case THEME_PART_TAB:
      fill_rect(get_sys_color(brControlBg), r);
      fill_rect(get_sys_color(brLightEdge), R(r.x, r.y, r.w - 1, 1));
      fill_rect(get_sys_color(brLightEdge), R(r.x, r.y, 1, r.h));
      fill_rect(get_sys_color(brDarkEdge), R(r.x + r.w - 1, r.y + 1, 1, r.h - 1));
      fill_rect(get_sys_color(brFlare), R(r.x, r.y, 1, 1));
      if (state & CTRL_SELECTED) fill_rect(get_sys_color(brControlBg), R(r.x + 2, r.y + r.h, r.w - 4, 2));
      break;
    case THEME_PART_TAB_PANE:
    case THEME_PART_PANEL_BORDER:        classic_draw_bevel(r); break;
    case THEME_PART_TOOLBAR:             classic_draw_bevel(r); fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_HEADER:              classic_draw_button_bg(r, state); break;
    case THEME_PART_RESIZE_GRIP:
      fill_rect(get_sys_color(brLightEdge), R(r.x+r.w, r.y+r.h-SCROLLBAR_WIDTH+1, 1, SCROLLBAR_WIDTH));
      fill_rect(get_sys_color(brLightEdge), R(r.x+r.w-SCROLLBAR_WIDTH+1, r.y+r.h, SCROLLBAR_WIDTH, 1));
      break;
    case THEME_PART_MENU_BAR:
      fill_rect(get_sys_color(brWindowDarkBg), r);
      fill_rect(get_sys_color(brDarkEdge), rect_split_bottom(r, 1));
      break;
    case THEME_PART_MENU_POPUP:          fill_rect(get_sys_color(brControlBg), r); draw_wire_rect(r, 0, get_sys_color(brDarkEdge)); break;
    case THEME_PART_SEPARATOR:
    case THEME_PART_SLIDER_TRACK:        fill_rect(get_sys_color(brDarkEdge), r); break;
    case THEME_PART_SCROLLBAR_TRACK:     fill_rect(get_sys_color(brWindowDarkBg), r); break;
    case THEME_PART_SCROLLBAR_THUMB:     fill_rect(get_sys_color(disabled ? brDarkEdge : brLightEdge), r); break;
    case THEME_PART_SCROLLBAR_ARROW_UP:
    case THEME_PART_SCROLLBAR_ARROW_DOWN:
    case THEME_PART_SCROLLBAR_ARROW_LEFT:
    case THEME_PART_SCROLLBAR_ARROW_RIGHT: {
      int icon = part == THEME_PART_SCROLLBAR_ARROW_UP ? THEME_ICON_SCROLL_UP :
                 part == THEME_PART_SCROLLBAR_ARROW_DOWN ? THEME_ICON_SCROLL_DOWN :
                 part == THEME_PART_SCROLLBAR_ARROW_LEFT ? THEME_ICON_SCROLL_LEFT : THEME_ICON_SCROLL_RIGHT;
      fill_rect(get_sys_color(brControlBg), r);
      draw_theme_icon_in_rect(icon, r, foreground);
      break;
    }
    case THEME_PART_SCROLLBAR_CORNER:
      fill_rect(get_sys_color(brWindowDarkBg), r);
      draw_theme_icon_in_rect(THEME_ICON_RESIZE, r, foreground);
      break;
    case THEME_PART_COUNT: break;
  }
}

static uint32_t classic_foreground(theme_part_t part, ctrl_state_t state) {
  if (state & CTRL_DISABLED) return get_sys_color(brTextDisabled);
  if (part == THEME_PART_LIST_ITEM && (state & CTRL_SELECTED)) return get_sys_color(brActiveTitlebarText);
  if (part == THEME_PART_MENU_ITEM && (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED)))
    return get_sys_color(brActiveTitlebarText);
  return get_sys_color(brTextNormal);
}

static void classic_draw_button_label(irect16_t r, const char *text, ctrl_state_t state) {
  bool pressed = (state & (CTRL_PRESSED | CTRL_SELECTED)) && !(state & CTRL_DISABLED);
  if (!pressed && !(state & CTRL_DISABLED))
    draw_text_small(text, r.x + TEXT_SHADOW_OFFSET, r.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
  if (pressed) r = rect_offset(r, 1, 1);
  draw_text_small(text, r.x, r.y, classic_foreground(THEME_PART_BUTTON, state));
}

static void classic_draw_combobox(irect16_t r, const char *text, ctrl_state_t state) {
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  classic_draw_combobox_bg(r, state);
  irect16_t arrow = rect_split_right(r, MIN(r.h, 16));
  irect16_t label = rect_inset_xy(rect_trim_right(r, arrow.w), 2, 0);
  uint32_t foreground = classic_foreground(THEME_PART_COMBOBOX, state);
  draw_text_clipped(FONT_SYSTEM, text, &label, foreground, TEXT_PADDING_LEFT);
  draw_theme_icon_in_rect(THEME_ICON_ARROW_UPDOWN, arrow, foreground);
}

static theme_t g_classic_theme = {
  .style                  = THEME_CLASSIC,
  .name                   = "Classic",
  .draw_button_label      = classic_draw_button_label,
  .draw_combobox          = classic_draw_combobox,
  .foreground             = classic_foreground,
  .draw_part              = classic_draw_part,
  .draw_window_chrome     = classic_draw_window_chrome,
  .draw_statusbar         = classic_draw_statusbar,
  .scrollbar_width        = SCROLLBAR_WIDTH,
  .scrollbar_overlay      = false,
  .press_icon_offset      = 1,
  .button_corner_radius   = 0,
  .window_corner_radius   = 0,
  .control_padding        = BUTTON_PADDING,
  .apply_palette          = classic_apply_palette,
};

theme_t *theme_classic_instance(void) { return &g_classic_theme; }
