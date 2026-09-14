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
    fill_rect(get_sys_color(brControlBg),     R(r.x+1, r.y+1, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare),         R(r.x, r.y, 1, 1));
  }
}

// Toolbar item background.  Classic preserves historical borderless appearance
// for ordinary labeled buttons; dropdown parts always draw a bevel.
static void classic_draw_toolbar_item_bg(irect16_t r, ctrl_state_t state,
                                         toolbar_item_variant_t variant) {
  if (variant == TOOLBAR_VARIANT_BUTTON_LABELED) return;
  classic_draw_button_bg(r, state);
}

// ── Toolbar separator ─────────────────────────────────────────────────────────

static void classic_draw_toolbar_separator(irect16_t r) {
  int mx = r.w / 2;
  fill_rect(get_sys_color(brDarkEdge),  R(mx,     2, 1, r.h - 4));
  fill_rect(get_sys_color(brLightEdge), R(mx + 1, 2, 1, r.h - 4));
}

// ── Panel ─────────────────────────────────────────────────────────────────────

static void classic_draw_panel_bg(irect16_t r, bool resize_grip) {
  classic_draw_bevel(r);
  if (resize_grip) {
    int sb = SCROLLBAR_WIDTH;
    fill_rect(get_sys_color(brLightEdge), R(r.x+r.w,      r.y+r.h-sb+1, 1,  sb));
    fill_rect(get_sys_color(brLightEdge), R(r.x+r.w-sb+1, r.y+r.h,      sb, 1));
  }
}

// ── Titlebar ─────────────────────────────────────────────────────────────────

static void classic_draw_titlebar_bg(irect16_t r, bool focused) {
  fill_rect(get_sys_color(focused ? brActiveTitlebar : brInactiveTitlebar), r);
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
    fill_rect(get_sys_color(brControlBg),    R(r.x+1, r.y+1, r.w-3, r.h-3));
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
  g_sys_colors[brActiveTitlebar]       = 0xffa05a1e;
  g_sys_colors[brActiveTitlebarText]   = 0xffffffff;
  g_sys_colors[brInactiveTitlebar]     = 0xff2c2c2c;
  g_sys_colors[brInactiveTitlebarText] = 0xff787878;
  g_sys_colors[brStatusbarBg]          = 0xff2c2c2c;
  g_sys_colors[brLightEdge]            = 0xff7f7f7f;
  g_sys_colors[brDarkEdge]             = 0xff1a1a1a;
  g_sys_colors[brFlare]                = 0xffcfcfcf;
  g_sys_colors[brAccent]               = 0xff5EC4F3;
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

static theme_t g_classic_theme = {
  .style                  = THEME_CLASSIC,
  .name                   = "Classic",
  .draw_bevel             = classic_draw_bevel,
  .draw_button_bg         = classic_draw_button_bg,
  .draw_toolbar_item_bg   = classic_draw_toolbar_item_bg,
  .draw_toolbar_separator = classic_draw_toolbar_separator,
  .draw_panel_bg          = classic_draw_panel_bg,
  .draw_titlebar_bg       = classic_draw_titlebar_bg,
  .draw_statusbar_bg      = classic_draw_statusbar_bg,
  .draw_checkbox_box      = classic_draw_checkbox_box,
  .draw_combobox_bg       = classic_draw_combobox_bg,
  .draw_list_item_bg      = classic_draw_list_item_bg,
  .draw_slider_thumb      = classic_draw_slider_thumb,
  .draw_menu_item_bg      = classic_draw_menu_item_bg,
  .scrollbar_width        = SCROLLBAR_WIDTH,
  .scrollbar_overlay      = false,
  .press_icon_offset      = 1,
  .button_corner_radius   = 0,
  .control_padding        = BUTTON_PADDING,
  .apply_palette          = classic_apply_palette,
};

theme_t *theme_classic_instance(void) { return &g_classic_theme; }
