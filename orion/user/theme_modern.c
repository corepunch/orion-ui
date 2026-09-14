// Modern theme — flat, quiet surfaces; borderless toolbar controls;
// rounded hover backgrounds.  Visual reference: Procreate / macOS / ChatGPT.
//
// Toolbar item states (per issue #216):
//   Normal    — icon/text only, no permanent frame
//   Hover     — soft rounded background within the hit target
//   Pressed   — stronger (darker) background
//   Selected  — persistent filled background, accent-tinted

#include <math.h>
#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>

// Radius constants (logical pixels).
#define RADIUS_TOOLBAR_ITEM  4
#define RADIUS_BUTTON        6
#define RADIUS_FIELD         8

// Accent-tinted fill for selected/active toolbar items.
// Derived from brAccent (cyan-blue) blended into the dark panel.
#define MODERN_SELECTED_BG       0xff3c4858
// Slightly brighter version used when selected + hovered simultaneously.
#define MODERN_SELECTED_HOVER_BG 0xff465266

// ── Helpers ──────────────────────────────────────────────────────────────────

// Filled rounded-rectangle using fill_rect() row-by-row.
// Radius is clamped to half the shortest dimension.
static void fill_rounded_rect(uint32_t color, irect16_t r, int radius) {
  if (r.w < 1 || r.h < 1) return;
  if (radius <= 0) { fill_rect(color, r); return; }
  int max_r = (r.w < r.h ? r.w : r.h) / 2;
  if (radius > max_r) radius = max_r;

  // Full-width center band.
  fill_rect(color, R(r.x, r.y + radius, r.w, r.h - 2 * radius));

  // Top and bottom rounded rows.
  for (int i = 0; i < radius; i++) {
    int dy = radius - 1 - i;
    int margin = radius - (int)(sqrtf((float)(radius * radius - dy * dy)) + 0.5f);
    fill_rect(color, R(r.x + margin, r.y + i,              r.w - 2*margin, 1));
    fill_rect(color, R(r.x + margin, r.y + r.h - 1 - i,   r.w - 2*margin, 1));
  }
}

// ── Bevel ────────────────────────────────────────────────────────────────────

static void modern_draw_bevel(irect16_t r) {
  // Modern: replace bevels with a single-pixel subdued separator on the
  // bottom and right edges only (gives depth without the retro 3-D look).
  fill_rect(get_sys_color(brWindowDarkBg), R(r.x, r.y+r.h, r.w+1, 1));
  fill_rect(get_sys_color(brWindowDarkBg), R(r.x+r.w, r.y, 1, r.h));
}

// ── Buttons ──────────────────────────────────────────────────────────────────

static void modern_draw_button_bg(irect16_t r, ctrl_state_t state) {
  if (state & CTRL_DISABLED) {
    fill_rounded_rect(get_sys_color(brWindowDarkBg), r, RADIUS_BUTTON);
    return;
  }
  uint32_t color;
  if (state & (CTRL_PRESSED | CTRL_SELECTED)) {
    color = get_sys_color(brWindowDarkBg);
  } else if (state & CTRL_HOVER) {
    color = get_sys_color(brButtonHover);
  } else if (state & CTRL_DEFAULT) {
    color = get_sys_color(brAccent);
  } else {
    color = get_sys_color(brButtonInner);
  }
  fill_rounded_rect(color, r, RADIUS_BUTTON);
}

// ── Toolbar items ─────────────────────────────────────────────────────────────

static void modern_draw_toolbar_item_bg(irect16_t r, ctrl_state_t state,
                                         toolbar_item_variant_t variant) {
  (void)variant;
  if (state & CTRL_DISABLED) return;  // disabled: no bg
  uint32_t color;
  if (state & CTRL_PRESSED) {
    color = get_sys_color(brWindowDarkBg);
  } else if ((state & (CTRL_SELECTED | CTRL_HOVER)) == (CTRL_SELECTED | CTRL_HOVER)) {
    // Selected + hover: still clearly selected, with subtle interaction feedback.
    color = MODERN_SELECTED_HOVER_BG;
  } else if (state & CTRL_SELECTED) {
    color = MODERN_SELECTED_BG;
  } else if (state & CTRL_HOVER) {
    color = get_sys_color(brButtonHover);
  } else {
    return;  // normal: no permanent background
  }
  fill_rounded_rect(color, r, RADIUS_TOOLBAR_ITEM);
}

// ── Toolbar separator ─────────────────────────────────────────────────────────

static void modern_draw_toolbar_separator(irect16_t r) {
  int mx = r.w / 2;
  fill_rect(get_sys_color(brWindowDarkBg), R(mx, 4, 1, r.h - 8));
}

// ── Panel ─────────────────────────────────────────────────────────────────────

static void modern_draw_panel_bg(irect16_t r, bool resize_grip) {
  // No bevel.  Subtle bottom separator instead.
  fill_rect(get_sys_color(brWindowDarkBg), R(r.x, r.y + r.h, r.w, 1));
  if (resize_grip) {
    // Minimal 3×3 dot grid in the bottom-right corner.
    uint32_t dot = get_sys_color(brTextDisabled);
    for (int row = 0; row < 3; row++) {
      for (int col = row; col < 3; col++) {
        fill_rect(dot, R(r.x + r.w - 6 + col*2, r.y + r.h - 6 + row*2, 1, 1));
      }
    }
  }
}

// ── Titlebar ─────────────────────────────────────────────────────────────────

static void modern_draw_titlebar_bg(irect16_t r, bool focused) {
  // Same color logic as Classic; chrome colors are already "modern" enough.
  fill_rect(get_sys_color(focused ? brActiveTitlebar : brInactiveTitlebar), r);
}

// ── Statusbar ────────────────────────────────────────────────────────────────

static void modern_draw_statusbar_bg(irect16_t r) {
  fill_rect(get_sys_color(brStatusbarBg), r);
}

// ── Checkbox ─────────────────────────────────────────────────────────────────

static void modern_draw_checkbox_box(irect16_t r, bool checked, ctrl_state_t state) {
  // Focus background: solid accent fill, only when keyboard-focused.
  if (state & CTRL_FOCUSED) {
    irect16_t focus_bg = rect_inset(r, -CHECKBOX_FOCUS_PAD);
    fill_rect(get_sys_color(brAccent), focus_bg);
  }

  uint32_t bg = (state & CTRL_HOVER) ? get_sys_color(brButtonHover)
                                      : get_sys_color(brWindowDarkBg);
  fill_rounded_rect(bg, r, 3);

  if (checked)
    draw_theme_icon_in_rect(THEME_ICON_CHECKMARK, r, get_sys_color(brAccent));
}

// ── Combobox ─────────────────────────────────────────────────────────────────

static void modern_draw_combobox_bg(irect16_t r, ctrl_state_t state) {
  modern_draw_button_bg(r, state);
}

// ── List item ────────────────────────────────────────────────────────────────

static void modern_draw_list_item_bg(irect16_t r, ctrl_state_t state) {
  if (state & CTRL_SELECTED)
    fill_rect(get_sys_color(brTextNormal), r);
}

// ── Slider thumb ─────────────────────────────────────────────────────────────

static void modern_draw_slider_thumb(irect16_t r, bool active) {
  fill_rect(active ? get_sys_color(brAccent) : get_sys_color(brDarkEdge), r);
  fill_rect(get_sys_color(brTextNormal), R(r.x+1, r.y+1, r.w-2, r.h-2));
}

// ── Menu item background ─────────────────────────────────────────────────────

static void modern_draw_menu_item_bg(irect16_t r, ctrl_state_t state) {
  if (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED))
    fill_rect(get_sys_color(brAccent), r);
}

// ── Palette ───────────────────────────────────────────────────────────────────

static void modern_apply_palette(void) {
  g_sys_colors[brTransparent]          = 0x00000000;
  g_sys_colors[brControlBg]            = 0xffF3F3F3;
  g_sys_colors[brWindowDarkBg]         = 0xffE8E8E8;
  g_sys_colors[brWorkspaceBg]          = 0xffDCDCDC;
  g_sys_colors[brActiveTitlebar]       = 0xff0078D4;
  g_sys_colors[brActiveTitlebarText]   = 0xffffffff;
  g_sys_colors[brInactiveTitlebar]     = 0xffF0F0F0;
  g_sys_colors[brInactiveTitlebarText] = 0xff767676;
  g_sys_colors[brStatusbarBg]          = 0xffF0F0F0;
  g_sys_colors[brLightEdge]            = 0xffffffff;
  g_sys_colors[brDarkEdge]             = 0xffC8C8C8;
  g_sys_colors[brFlare]                = 0xffffffff;
  g_sys_colors[brAccent]               = 0xff0078D4;
  g_sys_colors[brButtonInner]          = 0xffE0E0E0;
  g_sys_colors[brButtonHover]          = 0xffD0D0D0;
  g_sys_colors[brTextNormal]           = 0xff1A1A1A;
  g_sys_colors[brTextDisabled]         = 0xff9E9E9E;
  g_sys_colors[brTextError]            = 0xffC42B1C;
  g_sys_colors[brTextSuccess]          = 0xff0F7B0F;
  g_sys_colors[brBorderFocus]          = 0xff005FB8;
  g_sys_colors[brBorderActive]         = 0xff868686;
  g_sys_colors[brFolderText]           = 0xff107C10;
  g_sys_colors[brColumnViewBg]         = 0xffDEE3EA;
  g_sys_colors[brModalOverlay]         = 0x40000000;
  g_sys_colors[brToolbarForeground]    = 0xff1A1A1A;
}

// ── Singleton ────────────────────────────────────────────────────────────────

static theme_t g_modern_theme = {
  .style                  = THEME_MODERN,
  .name                   = "Modern",
  .draw_bevel             = modern_draw_bevel,
  .draw_button_bg         = modern_draw_button_bg,
  .draw_toolbar_item_bg   = modern_draw_toolbar_item_bg,
  .draw_toolbar_separator = modern_draw_toolbar_separator,
  .draw_panel_bg          = modern_draw_panel_bg,
  .draw_titlebar_bg       = modern_draw_titlebar_bg,
  .draw_statusbar_bg      = modern_draw_statusbar_bg,
  .draw_checkbox_box      = modern_draw_checkbox_box,
  .draw_combobox_bg       = modern_draw_combobox_bg,
  .draw_list_item_bg      = modern_draw_list_item_bg,
  .draw_slider_thumb      = modern_draw_slider_thumb,
  .draw_menu_item_bg      = modern_draw_menu_item_bg,
  .scrollbar_width        = 0,
  .scrollbar_overlay      = true,
  .press_icon_offset      = 0,
  .button_corner_radius   = RADIUS_BUTTON,
  .control_padding        = BUTTON_PADDING,
  .apply_palette          = modern_apply_palette,
};

theme_t *theme_modern_instance(void) { return &g_modern_theme; }
