// Modern theme — flat, borderless toolbar controls; rounded hover backgrounds.
// THEME_MODERN is the original light WinUI palette (quiet gray, accent caption).
// THEME_NAVY shares this drawing with a navy/purple palette. Drawing consults
// get_theme()->style so accent titlebars stay on Modern and navy toolbars stay
// on Navy.
//
// Toolbar item states (per issue #216):
//   Normal    — icon/text only, no permanent frame
//   Hover     — soft rounded background within the hit target
//   Pressed   — stronger (darker) background
//   Selected  — persistent filled background, accent-tinted

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>

// Radius constants (logical pixels).
#define RADIUS_TOOLBAR_ITEM  4
#define RADIUS_BUTTON        ((BUTTON_HEIGHT + 1) / 2)
#define RADIUS_FIELD         12
#define RADIUS_MENU_ITEM     6
#define MODERN_LIST_INSET_X  4
#define MODERN_LIST_INSET_Y  1

// Secondary-button resting border. Navy uses the visible-on-navy hairline
// (brLightEdge). Default keeps the original quiet gray outline.
#define MODERN_SECONDARY_BORDER        WEB(0x4A5C7A)
#define MODERN_DEFAULT_SECONDARY_BORDER  0xff767676

static bool modern_navy(void) { return get_theme()->style == THEME_NAVY; }

static uint32_t modern_secondary_border(void) {
  return modern_navy() ? MODERN_SECONDARY_BORDER : MODERN_DEFAULT_SECONDARY_BORDER;
}

// ── Buttons ──────────────────────────────────────────────────────────────────

static void modern_draw_button_bg(irect16_t r, ctrl_state_t state) {
  int radius = (MIN(r.w, r.h) + 1) / 2;
  if (state & CTRL_DISABLED) {
    fill_rounded_rect(get_sys_color(brWindowDarkBg), r, radius);
    return;
  }

  if (state & CTRL_DEFAULT) {
    // Primary button shares the active-state accent.
    uint32_t fill = get_sys_color(brAccent);
    fill_rounded_rect(fill, r, radius);
    return;
  }

  // Secondary button: 1-px outline; focus uses accent, rest uses the hairline.
  uint32_t border = (state & CTRL_FOCUSED) ? get_sys_color(brAccent)
                                           : modern_secondary_border();

  // Cover the interior after drawing the border.
  fill_rounded_rect(border, r, radius);
  if (state & (CTRL_PRESSED | CTRL_SELECTED)) {
    fill_rounded_rect(get_sys_color(brButtonHover), rect_inset(r, 1), radius - 1);
  } else if (state & CTRL_HOVER) {
    fill_rounded_rect(get_sys_color(brButtonInner), rect_inset(r, 1), radius - 1);
  } else {
    fill_rounded_rect(get_sys_color(brControlBg), rect_inset(r, 1), radius - 1);
  }
}

// ── Toolbar items ─────────────────────────────────────────────────────────────

static void modern_draw_toolbar_item_bg(irect16_t r, ctrl_state_t state,
                                         theme_part_t part) {
  if (state & CTRL_DISABLED) return;  // disabled: no bg
  uint32_t color;
  if (state & CTRL_PRESSED) {
    color = get_sys_color(theme_modern_instance()->item_background.pressed);
  } else if ((state & (CTRL_SELECTED | CTRL_HOVER)) == (CTRL_SELECTED | CTRL_HOVER)) {
    // Keep selection the same accent while hovered.
    color = get_sys_color(theme_modern_instance()->item_background.selected_hover);
  } else if (state & CTRL_SELECTED) {
    color = get_sys_color(theme_modern_instance()->item_background.selected);
  } else if (state & CTRL_HOVER) {
    color = get_sys_color(theme_modern_instance()->item_background.hover);
  } else {
    return;  // normal: no permanent background
  }
  if (part == THEME_PART_LIST_ITEM) {
    // Keep the row clickable edge-to-edge, but make the selected surface read
    // as a native rounded selection rather than a solid table stripe.
    r = rect_inset_xy(r, MODERN_LIST_INSET_X, MODERN_LIST_INSET_Y);
    fill_rounded_rect(color, r, RADIUS_MENU_ITEM);
  } else {
    fill_rounded_rect(color, r, RADIUS_TOOLBAR_ITEM);
  }
}

// ── Panel ─────────────────────────────────────────────────────────────────────

static void modern_draw_panel_bg(irect16_t r) {
  // Flat sidebar surface, with no boundary stroke.
  fill_rect(get_sys_color(brControlBg), r);

}

// ── Titlebar ─────────────────────────────────────────────────────────────────

static void modern_draw_titlebar_bg(irect16_t r, bool focused) {
  // Navy keeps titlebars on chrome (brActiveTitlebar), not the purple accent.
  // Default matches Classic: focused caption is the accent color.
  uint32_t color = modern_navy()
    ? get_sys_color(focused ? brActiveTitlebar : brInactiveTitlebar)
    : get_sys_color(focused ? brAccent : brInactiveTitlebar);
  fill_rect(color, r);
}

// ── Statusbar ────────────────────────────────────────────────────────────────

static void modern_draw_statusbar_bg(irect16_t r) {
  fill_rect(get_sys_color(brStatusbarBg), r);
}

// ── Checkbox ─────────────────────────────────────────────────────────────────

static void modern_draw_checkbox_box(irect16_t r, bool checked, ctrl_state_t state) {
  // Focus background: solid accent fill, only when keyboard-focused.
  if ((state & CTRL_FOCUSED) && !(state & CTRL_DISABLED)) {
    irect16_t focus_bg = rect_inset(r, -CHECKBOX_FOCUS_PAD);
    fill_rect(get_sys_color(brAccent), focus_bg);
  }

  uint32_t bg = (state & CTRL_DISABLED) ? get_sys_color(brWindowDarkBg) :
                (state & CTRL_HOVER)    ? get_sys_color(brButtonHover)  :
                                          get_sys_color(brWindowDarkBg);
  fill_rounded_rect(bg, r, 3);

  if (checked) {
    uint32_t check_col = (state & CTRL_DISABLED) ? get_sys_color(brTextDisabled)
                                                 : get_sys_color(brAccent);
    draw_theme_icon_in_rect(THEME_ICON_CHECKMARK, r, check_col);
  }
}

// ── Combobox ─────────────────────────────────────────────────────────────────

static uint32_t modern_surface_midpoint(uint32_t a, uint32_t b) {
  uint32_t color = 0xff000000;
  for (int shift = 0; shift < 24; shift += 8)
    color |= ((((a >> shift) & 0xff) + ((b >> shift) & 0xff)) / 2) << shift;
  return color;
}

static void modern_draw_field_bg(irect16_t r, ctrl_state_t state) {
  bool focused = (state & CTRL_FOCUSED) && !(state & CTRL_DISABLED);
  int radius = MIN(RADIUS_FIELD, MIN(r.w, r.h) / 2);
  uint32_t border, fill;
  if (modern_navy()) {
    border = focused ? get_sys_color(brAccent) : get_sys_color(brLightEdge);
    fill = get_sys_color(brWindowDarkBg);
  } else {
    uint32_t surface = get_sys_color(brControlBg);
    border = focused ? get_sys_color(brAccent)
                     : modern_surface_midpoint(surface, get_sys_color(brButtonInner));
    fill = modern_surface_midpoint(surface, get_sys_color(brWindowDarkBg));
  }
  fill_rounded_rect(border, r, radius);
  fill_rounded_rect(fill, rect_inset(r, 1), MAX(0, radius - 1));
}

// ── List item ────────────────────────────────────────────────────────────────

static void modern_draw_list_item_bg(irect16_t r, ctrl_state_t state) {
  modern_draw_toolbar_item_bg(r, state, THEME_PART_LIST_ITEM);
}

// ── Slider thumb ─────────────────────────────────────────────────────────────

static void modern_draw_slider_thumb(irect16_t r, bool active) {
  fill_rect(active ? get_sys_color(brAccent) : get_sys_color(brDarkEdge), r);
  fill_rect(get_sys_color(brTextNormal), R(r.x+1, r.y+1, r.w-2, r.h-2));
}

// ── Menu item background ─────────────────────────────────────────────────────

static void modern_draw_menu_item_bg(irect16_t r, ctrl_state_t state) {
  if (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED)) {
    r = rect_inset_xy(r, MENU_CAPSULE_INSET, 1);
    fill_rounded_rect(get_sys_color(brAccent), r, MIN(r.w, r.h) / 2);
  }
}

// ── Palette ───────────────────────────────────────────────────────────────────

static void modern_copy_palette(const uint32_t *src) {
  for (int i = 0; i < brCount; i++) g_sys_colors[i] = src[i];
}

static const uint32_t k_modern_default[brCount] = {
  [brTransparent]          = 0x00000000,
  [brControlBg]            = 0xffF3F3F3,
  [brWindowDarkBg]         = 0xffE8E8E8,
  [brWorkspaceBg]          = 0xffDCDCDC,
  [brActiveTitlebar]       = 0xffD77800,
  [brActiveTitlebarText]   = 0xffffffff,
  [brInactiveTitlebar]     = 0xffF0F0F0,
  [brInactiveTitlebarText] = 0xff767676,
  [brStatusbarBg]          = 0xffF0F0F0,
  [brLightEdge]            = 0xffffffff,
  [brDarkEdge]             = 0xffC8C8C8,
  [brFlare]                = 0xffffffff,
  [brAccent]               = 0xffD77800,
  [brButtonInner]          = 0xffE0E0E0,
  [brButtonHover]          = 0xffD0D0D0,
  [brTextNormal]           = 0xff1A1A1A,
  [brTextDisabled]         = 0xff9E9E9E,
  [brTextError]            = 0xffC42B1C,
  [brTextSuccess]          = 0xff0F7B0F,
  [brBorderFocus]          = 0xff005FB8,
  [brBorderActive]         = 0xff868686,
  [brFolderText]           = 0xff107C10,
  [brColumnViewBg]         = 0xffDEE3EA,
  [brModalOverlay]         = 0x40000000,
  [brToolbarForeground]    = 0xff1A1A1A,
};

static const uint32_t k_modern_navy[brCount] = {
  [brTransparent]          = 0x00000000,
  [brControlBg]            = WEB(0x17243B),
  [brWindowDarkBg]         = WEB(0x1A2942),
  [brWorkspaceBg]          = WEB(0x17243B),
  [brActiveTitlebar]       = WEB(0x19263E),
  [brActiveTitlebarText]   = WEB(0xF6F8FF),
  [brInactiveTitlebar]     = WEB(0x17243B),
  [brInactiveTitlebarText] = WEB(0x647089),
  [brStatusbarBg]          = WEB(0x19273E),
  [brLightEdge]            = WEB(0x4A5C7A),
  [brDarkEdge]             = WEB(0x2A3954),
  [brFlare]                = WEB(0xF8FAFF),
  [brAccent]               = WEB(0x7357F6),
  [brButtonInner]          = WEB(0x1B2942),
  [brButtonHover]          = WEB(0x243552),
  [brTextNormal]           = WEB(0xF6F8FF),
  [brTextDisabled]         = WEB(0x647089),
  [brTextError]            = WEB(0xC42B1C),
  [brTextSuccess]          = WEB(0x63C994),
  [brBorderFocus]          = WEB(0x8B70FF),
  [brBorderActive]         = WEB(0x2A3954),
  [brFolderText]           = WEB(0x4C91F5),
  [brColumnViewBg]         = WEB(0x1A2942),
  [brModalOverlay]         = (WEB(0x17243B) & 0x00ffffffu) | 0x40000000u,
  [brToolbarForeground]    = WEB(0xF6F8FF),
};

static void modern_apply_palette(void) { modern_copy_palette(k_modern_default); }
static void navy_apply_palette(void)   { modern_copy_palette(k_modern_navy); }

// ── Singleton ────────────────────────────────────────────────────────────────

static void modern_draw_window_chrome(irect16_t titlebar, irect16_t caption,
                                       const char *title, ctrl_state_t state, bool maximizable) {
  bool focused = (state & CTRL_FOCUSED) != 0;
  uint32_t title_color = get_sys_color(focused ? brActiveTitlebarText : brInactiveTitlebarText);
  modern_draw_titlebar_bg(titlebar, focused);
  irect16_t close = rect_split_right(caption, caption.h);
  if (!(state & CTRL_NO_CLOSE)) {
    draw_theme_icon_in_rect(THEME_ICON_CLOSE, close, title_color);
    caption = rect_trim_right(caption, close.w);
  }
  if (maximizable) {
    draw_theme_icon_in_rect(THEME_ICON_MAXIMIZE, rect_split_right(caption, caption.h),
                            title_color);
    caption = rect_trim_right(caption, caption.h);
  }
  draw_text_small_clipped(title, &caption, title_color, TEXT_PADDING_LEFT);
}

static void modern_draw_statusbar_text(irect16_t r, const char *text) {
  if (text) draw_text_clipped(FONT_SMALL, text, &r, get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
}

static void modern_draw_part(theme_part_t part, irect16_t r, ctrl_state_t state) {
  bool disabled = (state & CTRL_DISABLED) != 0;
  uint32_t foreground = get_sys_color(disabled ? brTextDisabled : brTextNormal);
  switch (part) {
    case THEME_PART_BUTTON:              modern_draw_button_bg(r, state); break;
    case THEME_PART_CHECKBOX:            modern_draw_checkbox_box(r, state & CTRL_SELECTED, state); break;
    case THEME_PART_COMBOBOX:            modern_draw_field_bg(r, state); break;
    case THEME_PART_COMBOBOX_ARROW:      draw_theme_icon_in_rect(THEME_ICON_ARROW_UPDOWN, r, foreground); break;
    case THEME_PART_TOOLBAR_BUTTON:
    case THEME_PART_TOOLBAR_LABELED_BUTTON:
    case THEME_PART_TOOLBAR_SPLIT_BUTTON:
    case THEME_PART_TOOLBAR_SPLIT_ARROW:
      modern_draw_toolbar_item_bg(r, state, part);
      if (part == THEME_PART_TOOLBAR_SPLIT_ARROW) {
        int cx = r.x + r.w / 2;
        int cy = r.y + r.h / 2 - 1 + ((state & CTRL_PRESSED) ? 0 : 0);
        uint32_t col = get_sys_color(disabled ? brTextDisabled : brToolbarForeground);
        for (int i = 0; i < 4; i++) fill_rect(col, R(cx - 3 + i, cy + i, 7 - 2 * i, 1));
      }
      break;
    case THEME_PART_TOOLBAR_SEPARATOR:   break;
    case THEME_PART_PANEL:               modern_draw_panel_bg(r); break;
    case THEME_PART_TITLEBAR:            modern_draw_titlebar_bg(r, state & CTRL_FOCUSED); break;
    case THEME_PART_WINDOW_CLOSE:        draw_theme_icon_in_rect(THEME_ICON_CLOSE, r, foreground); break;
    case THEME_PART_STATUSBAR:           modern_draw_statusbar_bg(r); break;
    case THEME_PART_LIST_ITEM:           modern_draw_list_item_bg(r, state); break;
    case THEME_PART_SLIDER_THUMB:        modern_draw_slider_thumb(r, state & CTRL_PRESSED); break;
    case THEME_PART_MENU_ITEM:           if (!disabled) modern_draw_menu_item_bg(r, state); break;
    case THEME_PART_SURFACE:             fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_FIELD:               modern_draw_field_bg(r, state); break;
    case THEME_PART_TAB:
      modern_draw_toolbar_item_bg(r, state, part);
      break;
    case THEME_PART_TAB_PANE:
    case THEME_PART_PANEL_BORDER:        break;
    case THEME_PART_TOOLBAR:
      fill_rect(get_sys_color(modern_navy() ? brActiveTitlebar : brControlBg), r);
      break;
    case THEME_PART_TOOLBAR_GRIP:
      if (r.w > r.h)
        fill_rect(get_sys_color(brTextDisabled), rect_center(r, MIN(16, r.w - 4), 2));
      else
        fill_rect(get_sys_color(brTextDisabled), rect_center(r, 2, MIN(16, r.h - 4)));
      break;
    case THEME_PART_HEADER:
      fill_rect(get_sys_color(brControlBg), r);
      fill_rect(get_sys_color(brButtonInner), rect_split_bottom(r, 1));
      break;
    case THEME_PART_RESIZE_GRIP:
      for (int row = 0; row < 3; row++)
        for (int col = row; col < 3; col++)
          fill_rect(get_sys_color(brTextDisabled), R(r.x+r.w-6+col*2, r.y+r.h-6+row*2, 1, 1));
      break;
    case THEME_PART_MENU_BAR:
      fill_rect(get_sys_color(modern_navy() ? brActiveTitlebar : brWindowDarkBg), r);
      break;
    case THEME_PART_MENU_POPUP:          fill_rounded_rect(get_sys_color(brControlBg), r, RADIUS_MENU_ITEM); break;
    case THEME_PART_SEPARATOR:           fill_rect(get_sys_color(brButtonInner), r); break;
    case THEME_PART_SLIDER_TRACK:
      fill_rect(get_sys_color(modern_navy() ? brLightEdge : brButtonInner), r);
      break;
    case THEME_PART_SCROLLBAR_TRACK:     fill_rect(get_sys_color(brStatusbarBg), r); break;
    case THEME_PART_SCROLLBAR_THUMB:
      r = rect_inset(r, (SCROLLBAR_WIDTH - SCROLLBAR_THUMB_WIDTH) / 2);
      fill_rounded_rect(get_sys_color(disabled ? brTextDisabled : brLightEdge), r, MIN(r.w, r.h) / 2);
      break;
    case THEME_PART_SCROLLBAR_ARROW_UP:
    case THEME_PART_SCROLLBAR_ARROW_DOWN:
    case THEME_PART_SCROLLBAR_ARROW_LEFT:
    case THEME_PART_SCROLLBAR_ARROW_RIGHT: {
      int icon = part == THEME_PART_SCROLLBAR_ARROW_UP ? THEME_ICON_SCROLL_UP :
                 part == THEME_PART_SCROLLBAR_ARROW_DOWN ? THEME_ICON_SCROLL_DOWN :
                 part == THEME_PART_SCROLLBAR_ARROW_LEFT ? THEME_ICON_SCROLL_LEFT : THEME_ICON_SCROLL_RIGHT;
      draw_theme_icon_in_rect(icon, r, foreground);
      break;
    }
    case THEME_PART_SCROLLBAR_CORNER:
      fill_rect(get_sys_color(brStatusbarBg), r);
      break;
    case THEME_PART_COUNT: break;
  }
}

static uint32_t modern_foreground(theme_part_t part, ctrl_state_t state) {
  if (state & CTRL_DISABLED) return get_sys_color(brTextDisabled);
  if ((state & CTRL_SELECTED) && (part == THEME_PART_LIST_ITEM || part == THEME_PART_TAB ||
      part == THEME_PART_TOOLBAR_BUTTON || part == THEME_PART_TOOLBAR_LABELED_BUTTON))
    return get_sys_color(brActiveTitlebarText);
  if (part == THEME_PART_BUTTON && (state & CTRL_DEFAULT))
    return get_sys_color(brActiveTitlebarText);
  if (part == THEME_PART_MENU_ITEM && (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED)))
    return get_sys_color(brActiveTitlebarText);
  return get_sys_color(brTextNormal);
}

static void modern_draw_button_label(irect16_t r, const char *text, ctrl_state_t state) {
  draw_text_small(text, r.x, r.y, modern_foreground(THEME_PART_BUTTON, state));
}

static void modern_draw_combobox(irect16_t r, const char *text, ctrl_state_t state) {
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  modern_draw_field_bg(r, state);
  int icon_size = MIN(COMBOBOX_ICON_SIZE, MAX(0, r.h - 4));
  int icon_padding = TEXTEDIT_PADDING_HORZ - icon_size / 4;
  irect16_t content = rect_inset_xy(r, TEXTEDIT_PADDING_HORZ, 0);
  irect16_t arrow = rect_center(rect_split_right(rect_trim_right(r, icon_padding),
                                              icon_size), icon_size, icon_size);
  irect16_t label = rect_trim_right(content, icon_size + icon_padding);
  uint32_t foreground = modern_foreground(THEME_PART_COMBOBOX, state);
  draw_text_clipped(FONT_SMALL, text, &label, foreground, 0);
  draw_theme_icon(THEME_ICON_ARROW_UPDOWN, arrow.x, arrow.y, icon_size, foreground);
}

static theme_t g_modern_theme = {
  .style                  = THEME_MODERN,
  .name                   = "Modern",
  .draw_button_label      = modern_draw_button_label,
  .draw_combobox          = modern_draw_combobox,
  .foreground             = modern_foreground,
  .draw_part              = modern_draw_part,
  .draw_window_chrome     = modern_draw_window_chrome,
  .draw_statusbar_text    = modern_draw_statusbar_text,
  .scrollbar_width        = SCROLLBAR_WIDTH,
  // Use the same reserved-space scrollbar geometry as Classic for now.  The
  // overlay/auto-hide treatment is deliberately deferred until it has a
  // complete input and layout contract.
  .scrollbar_overlay      = false,
  .press_icon_offset      = 0,
  .button_corner_radius   = RADIUS_BUTTON,
  .window_corner_radius   = 8,
  .window_shadow_blur     = 8,
  .window_shadow_offset   = {0, 4},
  .window_shadow_color    = 0x80000000,
  .item_background        = {
    .hover = brButtonHover, .selected = brAccent,
    .selected_hover = brAccent, .pressed = brAccent,
  },
  .control_padding        = BUTTON_PADDING,
  .apply_palette          = modern_apply_palette,
};

theme_t *theme_modern_instance(void) { return &g_modern_theme; }

theme_t *theme_navy_instance(void) {
  static theme_t g_navy_theme;
  static int ready;
  if (!ready) {
    g_navy_theme = g_modern_theme;
    g_navy_theme.style = THEME_NAVY;
    g_navy_theme.name = "Navy";
    g_navy_theme.apply_palette = navy_apply_palette;
    ready = 1;
  }
  return &g_navy_theme;
}
