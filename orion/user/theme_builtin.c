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
#include <orion/user/style.h>

#define MODERN_CAPTION_TEXT_INSET 8

// ── Window chrome and labels ─────────────────────────────────────────────────

void theme_draw_window_chrome(irect16_t titlebar, irect16_t caption,
                                       const char *title, ctrl_state_t state, bool maximizable) {
  bool focused = (state & CTRL_FOCUSED) != 0;
  uint32_t title_color = get_sys_color(focused ? brActiveTitlebarText : brInactiveTitlebarText);
  theme_css_draw_part(THEME_PART_TITLEBAR, titlebar, focused ? CTRL_FOCUSED : CTRL_NORMAL, NULL);
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
  draw_text_clipped("Text", title, &caption, title_color, 0);
}

void theme_draw_statusbar_text(irect16_t r, const char *text) {
  if (text) draw_text_clipped("StatusBar", text, &r, get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
}

// The text-shadow the sheet gives selector in state: none unless set. Offset and colour are the sheet's.
static bool text_shadow_of(const char *selector, ctrl_state_t state, ui_shadow_t *out) {
  const char *styled = ui_state_selector(selector, state);
  if (!ui_flag(styled, "text-shadow")) return false;
  *out = ui_shadow(styled, "text-shadow");
  return true;
}

void theme_draw_text_clipped(const char *selector, const char *text, irect16_t r, uint32_t color, ctrl_state_t state) {
  ui_shadow_t shadow;
  if (text_shadow_of(selector, state, &shadow)) {
    irect16_t at = rect_offset(r, shadow.offset.x, shadow.offset.y);
    draw_text_clipped(selector, text, &at, shadow.color, 0);
  }
  draw_text_clipped(selector, text, &r, color, 0);
}

void theme_draw_text_wrapped(const char *selector, const char *text, irect16_t r, uint32_t color, ctrl_state_t state) {
  ui_shadow_t shadow;
  if (text_shadow_of(selector, state, &shadow)) {
    irect16_t at = rect_offset(r, shadow.offset.x, shadow.offset.y);
    draw_text_wrapped_font(selector, text, &at, shadow.color);
  }
  draw_text_wrapped_font(selector, text, &r, color);
}

int theme_text_inset(const char *selector) {
  ui_shadow_t shadow;
  return text_shadow_of(selector, CTRL_NORMAL, &shadow) ? MAX(0, MAX(shadow.offset.x, shadow.offset.y)) : 0;
}

// The label of a button: engraved and shifted as the sheet says for its state
// (Button:active, Button:checked, Button:disabled). win is the button: its
// classes, #id and app sheet style the label.
void theme_draw_button_label(const window_t *win, irect16_t r, const char *text, ctrl_state_t state) {
  const char *button = ui_selector(win, "Button"), *styled = ui_state_selector(button, state);
  ui_shadow_t shadow;
  if (text_shadow_of(button, state, &shadow)) draw_text(button, text, r.x + shadow.offset.x, r.y + shadow.offset.y, shadow.color);
  int offset = ui_px(styled, "--label-offset");
  draw_text(button, text, r.x + offset, r.y + offset, theme_css_foreground(THEME_PART_BUTTON, state, ui_window_scope(win)));
}

void theme_draw_combobox(const window_t *win, irect16_t r, const char *text, ctrl_state_t state) {
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  theme_css_draw_part(THEME_PART_COMBOBOX, r, state, ui_window_scope(win));
  int icon_size = MIN(COMBOBOX_ICON_SIZE, MAX(0, r.h - 4));
  int icon_padding = TEXTEDIT_PADDING_HORZ - icon_size / 4;
  irect16_t content = rect_inset_xy(r, TEXTEDIT_PADDING_HORZ, 0);
  irect16_t arrow = rect_center(rect_split_right(rect_trim_right(r, icon_padding),
                                              icon_size), icon_size, icon_size);
  irect16_t label = rect_trim_right(content, icon_size + icon_padding);
  uint32_t foreground = theme_css_foreground(THEME_PART_COMBOBOX, state, ui_window_scope(win));
  draw_text_clipped(ui_selector(win, "ComboBox"), text, &label, foreground, 0);
  draw_theme_icon(THEME_ICON_ARROW_UPDOWN, arrow.x, arrow.y, icon_size, foreground);
}

static const theme_t k_modern_template = {
  .style                  = THEME_MODERN,
  .name                   = "Modern",
  .appearance             = { .dark = true, .counterpart = THEME_LIGHT },
};

static theme_t g_themes[THEME_LAST + 1];
static bool g_built[THEME_LAST + 1];

// The parsed sheets go stale when the application's sheet changes.
void theme_reset_instance_stylesheets(void);   // theme_css.c

void theme_reset_stylesheets(void) {
  theme_reset_instance_stylesheets();
  for (int i = 0; i <= THEME_LAST; i++) {
    style_sheet_free(g_themes[i].sheet);
    g_themes[i].sheet = NULL;
  }
}

static theme_t *css_theme(theme_style_t style, const char *name, bool dark, theme_style_t counterpart,
                          const char *origin) {
  theme_t *slot = &g_themes[style];
  if (!g_built[style]) {
    g_built[style] = true;
    *slot = k_modern_template;
    slot->style = style;
    slot->name = name;
    slot->appearance.dark = dark;
    slot->appearance.counterpart = counterpart;
    slot->css_origin = origin;
  }
  return slot;
}

theme_t *theme_modern_instance(void) {
  return css_theme(THEME_MODERN, "Modern", true, THEME_LIGHT, "orion/themes/modern.css");
}
theme_t *theme_light_instance(void) {
  return css_theme(THEME_LIGHT, "Light", false, THEME_MODERN, "orion/themes/modern.css");
}
theme_t *theme_navy_instance(void) {
  return css_theme(THEME_NAVY, "Navy", true, THEME_NAVY, "orion/themes/navy.css");
}
theme_t *theme_amber_instance(void) {
  return css_theme(THEME_AMBER, "Amber", false, THEME_AMBER, "orion/themes/amber.css");
}
theme_t *theme_porcelain_instance(void) {
  return css_theme(THEME_PORCELAIN, "Porcelain", false, THEME_PORCELAIN_DARK, "orion/themes/porcelain.css");
}
theme_t *theme_porcelain_dark_instance(void) {
  return css_theme(THEME_PORCELAIN_DARK, "Porcelain Dark", true, THEME_PORCELAIN, "orion/themes/porcelain.css");
}
// Classic: bevelled, square, compact chrome.
static const theme_t k_classic_template = {
  .name                   = "Classic",
};

theme_t *theme_classic_instance(void) {
  theme_t *classic = &g_themes[THEME_CLASSIC];
  if (!g_built[THEME_CLASSIC]) {
    g_built[THEME_CLASSIC] = true;
    *classic = k_classic_template;
    classic->style = THEME_CLASSIC;
    classic->appearance.dark = true;
    classic->appearance.counterpart = THEME_CLASSIC;
    classic->css_origin = "orion/themes/classic.css";
  }
  return classic;
}

theme_t *theme_metro_instance(void) {
  return css_theme(THEME_METRO, "Metro", true, THEME_METRO, "orion/themes/metro.css");
}
