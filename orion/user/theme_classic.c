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

// Toolbar item background — same bevel as a regular button.
static void classic_draw_toolbar_item_bg(irect16_t r, ctrl_state_t state) {
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

  fill_rect(get_sys_color(brDarkEdge),  r);
  fill_rect(get_sys_color(brLightEdge), R(r.x, r.y, r.w-1, r.h-1));
  fill_rect(get_sys_color(brDarkEdge),  R(r.x+1, r.y+1, r.w-2, r.h-2));
  fill_rect(get_sys_color(brWindowDarkBg), R(r.x+1, r.y+1, r.w-3, r.h-3));

  if (focused) {
    irect16_t fr = R(r.x - CHECKBOX_FOCUS_PAD, r.y - CHECKBOX_FOCUS_PAD,
                     r.w + 2*CHECKBOX_FOCUS_PAD, r.h + 2*CHECKBOX_FOCUS_PAD);
    draw_focused(fr);
  }
  if (checked) {
    draw_theme_icon_in_rect(THEME_ICON_CHECKMARK, r, get_sys_color(brTextNormal));
  }
}

// ── Singleton ────────────────────────────────────────────────────────────────

static theme_t g_classic_theme = {
  .style                 = THEME_CLASSIC,
  .name                  = "Classic",
  .draw_bevel            = classic_draw_bevel,
  .draw_button_bg        = classic_draw_button_bg,
  .draw_toolbar_item_bg  = classic_draw_toolbar_item_bg,
  .draw_toolbar_separator= classic_draw_toolbar_separator,
  .draw_panel_bg         = classic_draw_panel_bg,
  .draw_titlebar_bg      = classic_draw_titlebar_bg,
  .draw_statusbar_bg     = classic_draw_statusbar_bg,
  .draw_checkbox_box     = classic_draw_checkbox_box,
  .scrollbar_width       = SCROLLBAR_WIDTH,
  .scrollbar_overlay     = false,
  .press_icon_offset     = 1,
};

theme_t *theme_classic_instance(void) { return &g_classic_theme; }
