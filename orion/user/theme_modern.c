// Modern drawing — flat, borderless toolbar controls; rounded hover backgrounds.
// Palettes live in theme_palette_*.h. THEME_MODERN uses layered charcoal surfaces
// for creative workspaces. THEME_LIGHT is its light counterpart. THEME_NAVY
// is navy/purple. THEME_AMBER is a light business palette with white fields,
// softly rounded buttons and amber selection; THEME_PORCELAIN shares its metrics
// with cool white surfaces and indigo selection. Drawing consults get_theme()->style
// where a variant's surfaces differ; corner radii come from the theme metrics.
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
#include <orion/user/theme_palette_studio.h>
#include <orion/user/theme_palette_light.h>
#include <orion/user/theme_palette_navy.h>
#include <orion/user/theme_palette_amber.h>
#include <orion/user/theme_palette_porcelain.h>
#include <orion/user/theme_palette_porcelain_dark.h>

// Radius constants (logical pixels).
#define RADIUS_MENU_ITEM     6
#define MODERN_CAPTION_TEXT_INSET 8

// Secondary-button resting border. Navy uses the palette hairline;
// default keeps the original quiet gray outline.
#define MODERN_DEFAULT_SECONDARY_BORDER  0xff767676

#if defined(__APPLE__) && TARGET_OS_IOS
#define MODERN_MENUBAR_HEIGHT 29
#else
#define MODERN_MENUBAR_HEIGHT (FONT_SIZE + 12)
#endif

static bool modern_navy(void) { return get_theme()->style == THEME_NAVY; }
// Amber and Porcelain: wells with a hairline, business metrics.
// Porcelain: the merged title row shares the window face, like a unified macOS toolbar.
static bool modern_porcelain(void) {
  theme_style_t style = get_theme()->style;
  return style == THEME_PORCELAIN || style == THEME_PORCELAIN_DARK;
}

static bool modern_amber(void) {
  theme_style_t style = get_theme()->style;
  return style == THEME_AMBER || style == THEME_PORCELAIN || style == THEME_PORCELAIN_DARK;
}

static uint32_t modern_secondary_border(void) {
  return modern_navy() || modern_amber() ? get_sys_color(brLightEdge) : MODERN_DEFAULT_SECONDARY_BORDER;
}

// A corner radius metric applied to r: CORNER_RADIUS_CIRCULAR rounds the short side fully.
static int modern_radius(int metric, irect16_t r) {
  int full = (MIN(r.w, r.h) + 1) / 2;
  return metric == CORNER_RADIUS_CIRCULAR ? full : MIN(metric, full);
}

// ── Buttons ──────────────────────────────────────────────────────────────────

static void modern_draw_button_bg(irect16_t r, ctrl_state_t state) {
  int radius = modern_radius(get_theme()->button.corner_radius, r);
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

static void modern_draw_selection(irect16_t r, int radius) {
  fill_gradient_rounded_rect(get_sys_color(brSelectionTop), get_sys_color(brSelectionBottom), r, radius);
}

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
  if (state & (CTRL_SELECTED | CTRL_PRESSED)) modern_draw_selection(r, get_theme()->toolbar.corner_radius);
  else fill_rounded_rect(color, r, get_theme()->toolbar.corner_radius);
}

// ── Segmented control ────────────────────────────────────────────────────────

// A capsule inside the field-shaped track: selection gradient when chosen,
// a soft fill while hovered or pressed.
static void modern_draw_segment(irect16_t r, ctrl_state_t state) {
  int radius = (MIN(r.w, r.h) + 1) / 2;
  if (state & CTRL_SELECTED) {
    if (state & CTRL_DISABLED) fill_rounded_rect(get_sys_color(brButtonInner), r, radius);
    else modern_draw_selection(r, radius);
  } else if (state & CTRL_PRESSED) {
    fill_rounded_rect(get_sys_color(brButtonInner), r, radius);
  } else if (state & CTRL_HOVER) {
    fill_rounded_rect(get_sys_color(brButtonHover), r, radius);
  }
}

// ── Panel ─────────────────────────────────────────────────────────────────────

static void modern_draw_panel_bg(irect16_t r) {
  // Flat sidebar surface, with no boundary stroke.
  fill_rect(get_sys_color(brControlBg), r);

}

static void modern_draw_tab(irect16_t r, ctrl_state_t state) {
  uint32_t color = (state & CTRL_SELECTED) ? get_sys_color(brControlBg) :
                   (state & (CTRL_HOVER | CTRL_PRESSED)) ? get_sys_color(brButtonHover) :
                   get_sys_color(brPanelDark);
  uint32_t bottom = (state & CTRL_SELECTED) ? color : get_sys_color(brTabBarBg);
  fill_gradient_rounded_top_rect(color, bottom, r, get_theme()->tab.corner_radius);
}

// ── Titlebar ─────────────────────────────────────────────────────────────────

static void modern_draw_titlebar_bg(irect16_t r, bool focused) {
  // Win95/XP: active caption is a distinct colour (the accent in most palettes);
  // inactive is a dull bar.
  fill_rect(get_sys_color(focused ? brActiveTitlebar : brInactiveTitlebar), r);
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
                                                 : get_sys_color(brTextNormal);
    irect16_t check = rect_center(r, MIN(r.w, r.h), MIN(r.w, r.h));
    draw_theme_icon(THEME_ICON_CHECKMARK, check.x, check.y, check.w, check_col);
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
  int radius = (state & CTRL_MULTILINE) ? get_theme()->field.multiline_corner_radius
                                         : modern_radius(get_theme()->field.corner_radius, r);
  uint32_t border, fill;
  if (modern_amber()) {
    // White well like the lists, with a hairline that turns amber on focus.
    border = focused ? get_sys_color(brBorderFocus) : get_sys_color(brLightEdge);
    fill = get_sys_color(brColumnViewBg);
  } else if (modern_navy()) {
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
  // Reportview / table rows fill the full hit rect. Menus and dropdowns use
  // THEME_PART_MENU_ITEM (capsule), not this path.
  if (state & CTRL_DISABLED) return;
  uint32_t color;
  if (state & CTRL_PRESSED)
    color = get_sys_color(theme_modern_instance()->item_background.pressed);
  else if (state & CTRL_SELECTED)
    color = get_sys_color(theme_modern_instance()->item_background.selected);
  else if (state & CTRL_HOVER)
    color = get_sys_color(theme_modern_instance()->item_background.hover);
  else
    return;
  if ((state & CTRL_SELECTED) && (state & CTRL_INACTIVE) && !(state & CTRL_PRESSED))
    fill_rect(color_with_alpha(get_sys_color(brAccent), 0x26), r);
  else if (state & (CTRL_SELECTED | CTRL_PRESSED)) modern_draw_selection(r, 0);
  else fill_rect(color, r);
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
    modern_draw_selection(r, MIN(r.w, r.h) / 2);
  }
}

// ── Palette ───────────────────────────────────────────────────────────────────

static void modern_apply_palette(void) { theme_copy_palette(k_theme_palette_studio); }
static void light_apply_palette(void)  { theme_copy_palette(k_theme_palette_light); }
static void navy_apply_palette(void)   { theme_copy_palette(k_theme_palette_navy); }
static void amber_apply_palette(void)  { theme_copy_palette(k_theme_palette_amber); }
static void porcelain_apply_palette(void) { theme_copy_palette(k_theme_palette_porcelain); }
static void porcelain_dark_apply_palette(void) { theme_copy_palette(k_theme_palette_porcelain_dark); }

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
  caption = rect_trim_left(caption, MODERN_CAPTION_TEXT_INSET);
  draw_text_small_clipped(title, &caption, title_color, 0);
}

static void modern_draw_statusbar_text(irect16_t r, const char *text) {
  if (text) draw_text_clipped(FONT_SMALL, text, &r, get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
}

static void modern_draw_part(theme_part_t part, irect16_t r, ctrl_state_t state) {
  bool disabled = (state & CTRL_DISABLED) != 0;
  uint32_t foreground = get_sys_color(disabled ? brTextDisabled : brTextNormal);
  switch (part) {
    case THEME_PART_CARD:                break; // cards paint through draw_card
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
    case THEME_PART_COLUMN_DIVIDER:      fill_rect(get_sys_color((state & CTRL_HOVER) ? brAccent : brDarkEdge), r); break;
    case THEME_PART_PANEL:               modern_draw_panel_bg(r); break;
    case THEME_PART_SIDEBAR:
      fill_rect(get_sys_color(brPanelDark), r);
      fill_rect(get_sys_color(brDarkEdge), rect_split_right(r, 1));
      break;
    case THEME_PART_TITLEBAR:            modern_draw_titlebar_bg(r, state & CTRL_FOCUSED); break;
    case THEME_PART_WINDOW_CLOSE:        draw_theme_icon_in_rect(THEME_ICON_CLOSE, r, foreground); break;
    case THEME_PART_STATUSBAR:           modern_draw_statusbar_bg(r); break;
    case THEME_PART_LIST_ITEM:           modern_draw_list_item_bg(r, state); break;
    case THEME_PART_SLIDER_THUMB:        modern_draw_slider_thumb(r, state & CTRL_PRESSED); break;
    case THEME_PART_SEGMENTED_TRACK:     modern_draw_field_bg(r, state); break;
    case THEME_PART_SEGMENT:             modern_draw_segment(r, state); break;
    case THEME_PART_MENU_ITEM:           if (!disabled) modern_draw_menu_item_bg(r, state); break;
    case THEME_PART_SURFACE:             fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_FIELD:               modern_draw_field_bg(r, state); break;
    case THEME_PART_TAB:
      modern_draw_tab(r, state);
      break;
    case THEME_PART_TAB_BAR:             fill_rect(get_sys_color(brTabBarBg), r); break;
    case THEME_PART_PAGE:                fill_rect(get_sys_color(brPanelDarker), r); break;
    case THEME_PART_TAB_PANE:            fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_PANEL_BORDER:        break;
    case THEME_PART_WINDOW_BORDER:       break;
    case THEME_PART_TOOLBAR:
      fill_rect(get_sys_color(brPanelDarker), r);
      break;
    case THEME_PART_TITLE_TOOLBAR:
      if (!modern_porcelain()) { fill_rect(get_sys_color(brWindowDarkBg), r); break; }
      fill_rect(get_sys_color(brControlBg), r);
      fill_rect(get_sys_color(brDarkEdge), rect_split_bottom(r, 1));
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
      fill_rect(get_sys_color(brWindowDarkBg), r);
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
  if (part == THEME_PART_LIST_ITEM && (state & CTRL_INACTIVE)) return get_sys_color(brTextNormal);
  if (((state & CTRL_SELECTED) && (part == THEME_PART_LIST_ITEM || part == THEME_PART_SEGMENT)) ||
      ((state & (CTRL_SELECTED | CTRL_PRESSED)) && (part == THEME_PART_TOOLBAR_BUTTON ||
       part == THEME_PART_TOOLBAR_LABELED_BUTTON || part == THEME_PART_TOOLBAR_SPLIT_BUTTON)))
    return get_sys_color(brAccentText);
  if (part == THEME_PART_BUTTON && (state & CTRL_DEFAULT))
    return get_sys_color(brAccentText);
  if (part == THEME_PART_MENU_ITEM && (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED)))
    return get_sys_color(brAccentText);
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
  .appearance             = { .dark = true, .counterpart = THEME_LIGHT },
  .draw_button_label      = modern_draw_button_label,
  .draw_combobox          = modern_draw_combobox,
  .draw_card              = theme_default_draw_card,
  .foreground             = modern_foreground,
  .draw_part              = modern_draw_part,
  .draw_window_chrome     = modern_draw_window_chrome,
  .draw_statusbar_text    = modern_draw_statusbar_text,
  .scrollbar = { .width = SCROLLBAR_WIDTH, .overlay = false },
  .caption = { .height = TITLEBAR_HEIGHT },
  .menubar = { .height = MODERN_MENUBAR_HEIGHT },
  .toolbar = {
    .button_size = TB_SPACING, .padding = TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH, .spacing = 2, .corner_radius = 10,
    .compact = { .padding = 2, .spacing = 6, .icon = TOOLBAR_COMPACT_ICON_SIZE, .menu_gap = 8 },
    .grip_size = 12, .dropdown_arrow_w = 12, .large_inset = 2,
  },
  .tab = {
    .header = { .height = TITLEBAR_HEIGHT, .start = 0 },
    .min_width = 80, .padding = 16, .pane_frame = 0, .corner_radius = 6, .pane_margin = 2,
  },
  .menu = { .capsule = 1 },
  .report = { .divider_width = 1 },
  .splitter = { .spacing = 0 },
  .badge = { .padding = 7, .corner_radius = 5, .tint_alpha = 0x26, .ink_mix = 0x50, .icon_size = 12, .icon_gap = 4 },
  .tooltip = {
    .corner_radius = 6, .tail_size = 6, .padding_x = 8, .padding_y = 5, .gap = 2,
    .shadow = { .size = 10, .blur = 2, .offset = {0, 3}, .color = 0x78000000 },
  },
  .speechbubble = {
    .corner_radius = 5, .tail_width = 10, .tail_length = 16,
    .shadow = { .size = 2, .blur = 1, .offset = {1, 1}, .color = 0x40000000 },
  },
  .command_link = { .bullet_size = 6, .gap = 8, .padding_y = 2 },
  .field = { .corner_radius = CORNER_RADIUS_CIRCULAR, .multiline_corner_radius = (CONTROL_HEIGHT_REGULAR + 1) / 2,
             .padding_x = TEXTEDIT_PADDING_CAPSULE },
  .control = { .padding = BUTTON_PADDING, .press_icon_offset = 0, .size_pct = { 100, 84, 71, 133 } },
  .button = { .corner_radius = CORNER_RADIUS_CIRCULAR },
  .window = {
    .corner_radius = 8,
    .shadow = { .blur = 8, .offset = {0, 4}, .color = 0x80000000 },
  },
  .table = { .corner_radius = 6 },
  .card = {
    .corner_radius = 7, .edge_width = 4, .ring_width = 1, .highlight_width = 1, .shadow_size = 1,
    .padding_x = 12, .padding_y = 8,
  },
  .plastic = {
    .corner_radius = CORNER_RADIUS_CIRCULAR, .bevel_width = 2, .shadow_size = 3,
    .gloss = 1.0f, .rim = 0.55f, .ink = 0.10f, .lift = 0.10f, .glyph_size = 20, .shadow_color = 0xE0000000,
  },
  .drag = {
    .shadow = { .blur = 6, .offset = {0, 8}, .color = 0xE0000000 },
  },
  .item_background        = {
    .hover = brButtonHover, .selected = brAccent,
    .selected_hover = brAccent, .pressed = brAccent,
  },
  .apply_palette          = modern_apply_palette,
};

theme_t *theme_modern_instance(void) { return &g_modern_theme; }

static theme_t *modern_variant(theme_style_t style, const char *name, bool dark,
                                theme_style_t counterpart, void (*apply)(void)) {
  static theme_t variants[THEME_LAST + 1];
  theme_t *slot = &variants[style];
  if (!slot->draw_part) {
    *slot = g_modern_theme;
    if (style != THEME_LIGHT) {
      slot->toolbar.spacing = TOOLBAR_SPACING;
      slot->toolbar.corner_radius = 4;
    }
    slot->style = style;
    slot->name = name;
    slot->appearance.dark = dark;
    slot->appearance.counterpart = counterpart;
    slot->apply_palette = apply;
  }
  return slot;
}

theme_t *theme_light_instance(void) {
  return modern_variant(THEME_LIGHT, "Light", false, THEME_MODERN, light_apply_palette);
}

theme_t *theme_navy_instance(void) {
  return modern_variant(THEME_NAVY, "Navy", true, THEME_NAVY, navy_apply_palette);
}

// Business metrics: softly rounded rectangles instead of capsules, like a business form.
static theme_t *business_variant(theme_style_t style, const char *name, bool dark,
                                 theme_style_t counterpart, void (*apply)(void)) {
  theme_t *theme = modern_variant(style, name, dark, counterpart, apply);
  theme->button.corner_radius = 4;
  theme->field.corner_radius = 3;
  theme->field.multiline_corner_radius = 3;
  theme->field.padding_x = FIELD_PADDING_X(theme->field.corner_radius);
  theme->tab.corner_radius = 4;
  theme->card.corner_radius = 5;
  theme->window.corner_radius = 6;
  return theme;
}

theme_t *theme_amber_instance(void)     { return business_variant(THEME_AMBER, "Amber", false, THEME_AMBER, amber_apply_palette); }
// Porcelain rounds a little more than the business forms: 6 px buttons, 5 px
// fields, 7 px cards, as its design concepts draw them.
static theme_t *porcelain_variant(theme_style_t style, const char *name, bool dark,
                                  theme_style_t counterpart, void (*apply)(void)) {
  theme_t *theme = business_variant(style, name, dark, counterpart, apply);
  theme->button.corner_radius = 6;
  theme->field.corner_radius = 5;
  theme->field.multiline_corner_radius = 5;
  theme->field.padding_x = FIELD_PADDING_X(theme->field.corner_radius);
  theme->tab.corner_radius = 5;
  theme->card.corner_radius = 7;
  theme->card.face = brColumnViewBg;    // white panels with a hairline on the window face
  theme->card.outline = brLightEdge;
  theme->table.corner_radius = 6;
  theme->report.row_height = 21;   // the concepts' 21 px rows under a 24 px header, ruled by hairlines
  theme->report.header_height = 24;
  theme->report.row_dividers = true;
  return theme;
}

theme_t *theme_porcelain_instance(void) {
  return porcelain_variant(THEME_PORCELAIN, "Porcelain", false, THEME_PORCELAIN_DARK, porcelain_apply_palette);
}
theme_t *theme_porcelain_dark_instance(void) {
  return porcelain_variant(THEME_PORCELAIN_DARK, "Porcelain Dark", true, THEME_PORCELAIN, porcelain_dark_apply_palette);
}
