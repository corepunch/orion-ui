// The built-in themes. Every theme paints with the CSS box painter (theme_box.c)
// over orion/themes/base.css and its own sheet: Modern and Light are
// modern.css in the dark and light appearance; Navy, Amber, Porcelain (Light
// and Dark), Metro and Classic have their own sheets. This file keeps the
// window chrome, the labels and the instances; every part, colour and radius
// comes from the sheets (docs/styling.md). The templates hold the chrome
// geometry the sheets do not set yet.

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "orion/themes/modern_css.h"
#include "orion/themes/navy_css.h"
#include "orion/themes/amber_css.h"
#include "orion/themes/porcelain_css.h"
#include "orion/themes/metro_css.h"
#include "orion/themes/classic_css.h"

#define MODERN_CAPTION_TEXT_INSET 8

#if defined(__APPLE__) && TARGET_OS_IOS
#define MODERN_MENUBAR_HEIGHT 29
#else
#define MODERN_MENUBAR_HEIGHT (FONT_SIZE + 12)
#endif

// ── Window chrome and labels ─────────────────────────────────────────────────

static void modern_draw_window_chrome(irect16_t titlebar, irect16_t caption,
                                       const char *title, ctrl_state_t state, bool maximizable) {
  bool focused = (state & CTRL_FOCUSED) != 0;
  uint32_t title_color = get_sys_color(focused ? brActiveTitlebarText : brInactiveTitlebarText);
  theme_css_draw_part(THEME_PART_TITLEBAR, titlebar, focused ? CTRL_FOCUSED : CTRL_NORMAL);
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

// Bevelled themes engrave the label and shift it while the button is down.
static void modern_draw_button_label(irect16_t r, const char *text, ctrl_state_t state) {
  const theme_t *theme = get_theme();
  bool pressed = (state & (CTRL_PRESSED | CTRL_SELECTED)) && !(state & CTRL_DISABLED);
  if (theme->text_shadow && !pressed && !(state & CTRL_DISABLED))
    draw_text_small(text, r.x + TEXT_SHADOW_OFFSET, r.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
  if (pressed) r = rect_offset(r, theme->control.press_icon_offset, theme->control.press_icon_offset);
  draw_text_small(text, r.x, r.y, theme_css_foreground(THEME_PART_BUTTON, state));
}

static void modern_draw_combobox(irect16_t r, const char *text, ctrl_state_t state) {
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  theme_css_draw_part(THEME_PART_COMBOBOX, r, state);
  int icon_size = MIN(COMBOBOX_ICON_SIZE, MAX(0, r.h - 4));
  int icon_padding = TEXTEDIT_PADDING_HORZ - icon_size / 4;
  irect16_t content = rect_inset_xy(r, TEXTEDIT_PADDING_HORZ, 0);
  irect16_t arrow = rect_center(rect_split_right(rect_trim_right(r, icon_padding),
                                              icon_size), icon_size, icon_size);
  irect16_t label = rect_trim_right(content, icon_size + icon_padding);
  uint32_t foreground = theme_css_foreground(THEME_PART_COMBOBOX, state);
  draw_text_clipped(FONT_SMALL, text, &label, foreground, 0);
  draw_theme_icon(THEME_ICON_ARROW_UPDOWN, arrow.x, arrow.y, icon_size, foreground);
}

// Metrics the sheets do not set: chrome heights, toolbar, tooltip and slider geometry.
static const theme_t k_modern_template = {
  .style                  = THEME_MODERN,
  .name                   = "Modern",
  .appearance             = { .dark = true, .counterpart = THEME_LIGHT },
  .draw_button_label      = modern_draw_button_label,
  .draw_combobox          = modern_draw_combobox,
  .draw_card              = theme_default_draw_card,
  .foreground             = theme_css_foreground,
  .draw_part              = theme_css_draw_part,
  .draw_window_chrome     = modern_draw_window_chrome,
  .draw_statusbar_text    = modern_draw_statusbar_text,
  .scrollbar = { .width = SCROLLBAR_WIDTH, .overlay = false },
  .caption = { .height = TITLEBAR_HEIGHT },
  .menubar = { .height = MODERN_MENUBAR_HEIGHT },
  .toolbar = {
    .button_size = TB_SPACING, .padding = TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH, .spacing = 2, .corner_radius = 4,
    .compact = { .padding = 2, .spacing = 6, .icon = TOOLBAR_COMPACT_ICON_SIZE, .menu_gap = 8 },
    .grip_size = 12, .dropdown_arrow_w = 12,
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
  .control = { .padding = BUTTON_PADDING, .press_icon_offset = 0 },
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
  .thumbnail = { .gap = 2, .ring_width = 3 },
  .slider = { .trough_inset = 3, .trough_radius = 9, .thumb_inset = 3, .thumb_length = 12, .thumb_radius = 6 },
  .plastic = {
    .gloss = 1.0f, .rim = 0.55f, .lift = 0.10f, .shadow_color = 0xE0000000,
  },
  .drag = {
    .shadow = { .blur = 6, .offset = {0, 8}, .color = 0xE0000000 },
  },
  .item_background        = {
    .hover = brButtonHover, .selected = brAccent,
    .selected_hover = brAccent, .pressed = brAccent,
  },
  .apply_palette          = theme_css_apply_palette,
};

static theme_t *css_theme(theme_style_t style, const char *name, bool dark, theme_style_t counterpart,
                          const char *css, const char *origin) {
  static theme_t themes[THEME_LAST + 1];
  static bool built[THEME_LAST + 1];
  theme_t *slot = &themes[style];
  if (!built[style]) {
    built[style] = true;
    *slot = k_modern_template;
    slot->style = style;
    slot->name = name;
    slot->appearance.dark = dark;
    slot->appearance.counterpart = counterpart;
    slot->css = css;
    slot->css_origin = origin;
    theme_init_from_stylesheet(slot);
  }
  return slot;
}

theme_t *theme_modern_instance(void) {
  return css_theme(THEME_MODERN, "Modern", true, THEME_LIGHT, k_modern_css, "orion/themes/modern.css");
}
theme_t *theme_light_instance(void) {
  return css_theme(THEME_LIGHT, "Light", false, THEME_MODERN, k_modern_css, "orion/themes/modern.css");
}
theme_t *theme_navy_instance(void) {
  return css_theme(THEME_NAVY, "Navy", true, THEME_NAVY, k_navy_css, "orion/themes/navy.css");
}
theme_t *theme_amber_instance(void) {
  return css_theme(THEME_AMBER, "Amber", false, THEME_AMBER, k_amber_css, "orion/themes/amber.css");
}
theme_t *theme_porcelain_instance(void) {
  return css_theme(THEME_PORCELAIN, "Porcelain", false, THEME_PORCELAIN_DARK, k_porcelain_css, "orion/themes/porcelain.css");
}
theme_t *theme_porcelain_dark_instance(void) {
  return css_theme(THEME_PORCELAIN_DARK, "Porcelain Dark", true, THEME_PORCELAIN, k_porcelain_css, "orion/themes/porcelain.css");
}
// Classic: bevelled, square, compact chrome.
static const theme_t k_classic_template = {
  .name                   = "Classic",
  .text_shadow            = true,
  .draw_button_label      = modern_draw_button_label,
  .draw_combobox          = modern_draw_combobox,
  .draw_card              = theme_default_draw_card,
  .foreground             = theme_css_foreground,
  .draw_part              = theme_css_draw_part,
  .draw_window_chrome     = modern_draw_window_chrome,
  .draw_statusbar_text    = modern_draw_statusbar_text,
  .scrollbar = { .width = 13, .overlay = false },
  .caption = { .height = FONT_SIZE + 5 },
  .menubar = { .height = FONT_SIZE + 5 },
  .toolbar = {
    .button_size = SYSICON_SIZE + 4, .padding = TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH,
    .spacing = TOOLBAR_SPACING, .corner_radius = 0,
    .compact = { .padding = 2, .spacing = 6, .icon = TOOLBAR_COMPACT_ICON_SIZE, .menu_gap = 8 },
    .grip_size = 12, .dropdown_arrow_w = 12,
  },
  .tab = {
    .header = { .height = TITLEBAR_HEIGHT, .start = 0 },
    .min_width = 80, .padding = 16, .pane_frame = 2, .pane_margin = 2,
  },
  .menu = { .capsule = 0 },
  .report = { .divider_width = 1 },
  .splitter = { .spacing = 2 },
  .badge = { .padding = 7, .corner_radius = 5, .tint_alpha = 0x40, .ink_mix = 0x30, .icon_size = 12, .icon_gap = 4 },
  .tooltip = {
    .corner_radius = 4, .tail_size = 6, .padding_x = 8, .padding_y = 5, .gap = 2,
    .shadow = { .size = 10, .blur = 2, .offset = {0, 3}, .color = 0x78000000 },
  },
  .speechbubble = {
    .corner_radius = 5, .tail_width = 10, .tail_length = 16,
    .shadow = { .size = 2, .blur = 1, .offset = {1, 1}, .color = 0x40000000 },
  },
  .command_link = { .bullet_size = 7, .gap = 6, .padding_y = 1 },
  .control = { .padding = BUTTON_PADDING, .press_icon_offset = 1 },
  .button = { .corner_radius = 0 },
  .field = { .padding_x = TEXTEDIT_PADDING_CAPSULE },
  .window = { .corner_radius = 0 },
  .card = {
    .corner_radius = 0, .edge_width = 4, .ring_width = 1, .highlight_width = 0, .shadow_size = 1,
    .padding_x = 8, .padding_y = 6,
  },
  .thumbnail = { .gap = 2, .ring_width = 2 },
  .slider = { .trough_inset = 3, .trough_radius = 0, .thumb_inset = 2, .thumb_length = 12, .thumb_radius = 0 },
  .plastic = { .gloss = 0.7f, .rim = 0.7f, .lift = 0.08f, .shadow_color = 0xE0000000 },
  .drag = { .shadow = { .blur = 6, .offset = {0, 8}, .color = 0xE0000000 } },
  .apply_palette          = theme_css_apply_palette,
};

theme_t *theme_classic_instance(void) {
  static theme_t classic;
  static bool built;
  if (!built) {
    built = true;
    classic = k_classic_template;
    classic.style = THEME_CLASSIC;
    classic.appearance.dark = true;
    classic.appearance.counterpart = THEME_CLASSIC;
    classic.css = k_classic_css;
    classic.css_origin = "orion/themes/classic.css";
    theme_init_from_stylesheet(&classic);
    classic.field.padding_x = TEXTEDIT_PADDING_CAPSULE;   // bevelled fields keep the wide inset
  }
  return &classic;
}

theme_t *theme_metro_instance(void) {
  return css_theme(THEME_METRO, "Metro", true, THEME_METRO, k_metro_css, "orion/themes/metro.css");
}
