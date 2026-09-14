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

// Secondary-button resting border (#767676) — quieter than full accent.
#define MODERN_SECONDARY_BORDER  0xff767676
// Inspector/sidebar panel background — slightly lighter than window bg.
#define MODERN_PANEL_BG          0xffF5F5F5
// List selection: #0078D4 at 20 % alpha blended over the row background.
#define MODERN_LIST_ACCENT       0x330078D4

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

// ── Buttons ──────────────────────────────────────────────────────────────────

static void modern_draw_button_bg(irect16_t r, ctrl_state_t state) {
  if (state & CTRL_DISABLED) {
    fill_rounded_rect(get_sys_color(brWindowDarkBg), r, RADIUS_BUTTON);
    return;
  }

  if (state & CTRL_DEFAULT) {
    // Primary button: solid accent fill, no border; darker shade indicates press.
    uint32_t fill = (state & CTRL_PRESSED) ? get_sys_color(brBorderFocus)
                                           : get_sys_color(brAccent);
    fill_rounded_rect(fill, r, RADIUS_BUTTON);
    return;
  }

  // Secondary button: 1-px outline border; transparent rest, light tint on hover/press.
  // Focus overrides border color to accent (#0078D4); otherwise subdued #767676.
  uint32_t border = (state & CTRL_FOCUSED) ? get_sys_color(brAccent)
                                           : MODERN_SECONDARY_BORDER;

  // Cover the interior after drawing the border.
  fill_rounded_rect(border, r, RADIUS_BUTTON);
  if (state & (CTRL_PRESSED | CTRL_SELECTED)) {
    fill_rounded_rect(get_sys_color(brButtonHover), rect_inset(r, 1), RADIUS_BUTTON - 1);
  } else if (state & CTRL_HOVER) {
    fill_rounded_rect(get_sys_color(brButtonInner), rect_inset(r, 1), RADIUS_BUTTON - 1);
  } else {
    fill_rounded_rect(get_sys_color(brControlBg), rect_inset(r, 1), RADIUS_BUTTON - 1);
  }
}

// ── Toolbar items ─────────────────────────────────────────────────────────────

static void modern_draw_toolbar_item_bg(irect16_t r, ctrl_state_t state,
                                         theme_part_t part) {
  (void)part;
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
  int mx  = r.x + r.w / 2;
  int pad = BUTTON_PADDING / 2;  // theme control_padding drives vertical inset
  fill_rect(get_sys_color(brButtonInner), R(mx, r.y + pad, 1, r.h - 2*pad));
}

// ── Panel ─────────────────────────────────────────────────────────────────────

static void modern_draw_panel_bg(irect16_t r) {
  // Lighter surface (#F5F5F5) distinguishes inspector/sidebar panels from
  // the window background without a bevel; a 1-px bottom edge in #E0E0E0
  // gives a soft boundary.
  fill_rect(MODERN_PANEL_BG, r);
  fill_rect(get_sys_color(brButtonInner), R(r.x, r.y + r.h, r.w, 1));

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

static void modern_draw_combobox_bg(irect16_t r, ctrl_state_t state) {
  modern_draw_toolbar_item_bg(r, state, THEME_PART_COMBOBOX);
  if ((state & CTRL_FOCUSED) && !(state & CTRL_DISABLED))
    draw_wire_rect(rect_inset(r, 1), 0, get_sys_color(brAccent));
}

// ── List item ────────────────────────────────────────────────────────────────

static void modern_draw_list_item_bg(irect16_t r, ctrl_state_t state) {
  // Semi-transparent accent blend (~20 % #0078D4) instead of an opaque
  // highlight fill so the row content remains legible over any bg texture.
  if (state & CTRL_SELECTED)
    fill_rect(MODERN_LIST_ACCENT, r);
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

static void modern_draw_window_chrome(irect16_t titlebar, irect16_t caption,
                                       const char *title, ctrl_state_t state) {
  bool focused = (state & CTRL_FOCUSED) != 0;
  modern_draw_titlebar_bg(titlebar, focused);
  irect16_t close = rect_split_right(caption, caption.h);
  draw_theme_icon_in_rect(THEME_ICON_CLOSE, close, get_sys_color(brTextNormal));
  caption = rect_trim_right(caption, close.w);
  draw_text_small_clipped(title, &caption,
      get_sys_color(focused ? brActiveTitlebarText : brInactiveTitlebarText), TEXT_PADDING_LEFT);
}

static void modern_draw_statusbar(irect16_t r, const char *text) {
  modern_draw_statusbar_bg(r);
  if (text) draw_text_clipped(FONT_SMALL, text, &r, get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
}

static void modern_draw_part(theme_part_t part, irect16_t r, ctrl_state_t state) {
  bool disabled = (state & CTRL_DISABLED) != 0;
  uint32_t foreground = get_sys_color(disabled ? brTextDisabled : brTextNormal);
  switch (part) {
    case THEME_PART_BUTTON:              modern_draw_button_bg(r, state); break;
    case THEME_PART_CHECKBOX:            modern_draw_checkbox_box(r, state & CTRL_SELECTED, state); break;
    case THEME_PART_COMBOBOX:            modern_draw_combobox_bg(r, state); break;
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
    case THEME_PART_TOOLBAR_SEPARATOR:   modern_draw_toolbar_separator(r); break;
    case THEME_PART_PANEL:               modern_draw_panel_bg(r); break;
    case THEME_PART_TITLEBAR:            modern_draw_titlebar_bg(r, state & CTRL_FOCUSED); break;
    case THEME_PART_WINDOW_CLOSE:        draw_theme_icon_in_rect(THEME_ICON_CLOSE, r, foreground); break;
    case THEME_PART_STATUSBAR:           modern_draw_statusbar_bg(r); break;
    case THEME_PART_LIST_ITEM:           modern_draw_list_item_bg(r, state); break;
    case THEME_PART_SLIDER_THUMB:        modern_draw_slider_thumb(r, state & CTRL_PRESSED); break;
    case THEME_PART_MENU_ITEM:           if (!disabled) modern_draw_menu_item_bg(r, state); break;
    case THEME_PART_SURFACE:             fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_FIELD:
      fill_rounded_rect(get_sys_color((state & CTRL_FOCUSED) && !disabled ? brAccent : brButtonInner), r, RADIUS_FIELD);
      fill_rounded_rect(get_sys_color(brWindowDarkBg), rect_inset(r, 1), RADIUS_FIELD - 1);
      break;
    case THEME_PART_TAB:
      if (state & CTRL_SELECTED) fill_rounded_rect(get_sys_color(brButtonInner), r, RADIUS_BUTTON);
      else if ((state & CTRL_HOVER) && !disabled) fill_rounded_rect(get_sys_color(brButtonHover), r, RADIUS_BUTTON);
      break;
    case THEME_PART_TAB_PANE:
    case THEME_PART_PANEL_BORDER:        break;
    case THEME_PART_TOOLBAR:             fill_rect(get_sys_color(brControlBg), r); break;
    case THEME_PART_HEADER:
      fill_rect(get_sys_color(brControlBg), r);
      fill_rect(get_sys_color(brButtonInner), rect_split_bottom(r, 1));
      break;
    case THEME_PART_RESIZE_GRIP:
      for (int row = 0; row < 3; row++)
        for (int col = row; col < 3; col++)
          fill_rect(get_sys_color(brTextDisabled), R(r.x+r.w-6+col*2, r.y+r.h-6+row*2, 1, 1));
      break;
    case THEME_PART_MENU_BAR:            fill_rect(get_sys_color(brWindowDarkBg), r); break;
    case THEME_PART_MENU_POPUP:          fill_rounded_rect(get_sys_color(brControlBg), r, RADIUS_FIELD); break;
    case THEME_PART_SEPARATOR:
    case THEME_PART_SLIDER_TRACK:        fill_rect(get_sys_color(brButtonInner), r); break;
    case THEME_PART_SCROLLBAR_TRACK:     break;
    case THEME_PART_SCROLLBAR_THUMB:     fill_rounded_rect(get_sys_color(disabled ? brTextDisabled : brLightEdge), r, MIN(r.w, r.h) / 2); break;
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
      break;
    case THEME_PART_COUNT: break;
  }
}

static uint32_t modern_foreground(theme_part_t part, ctrl_state_t state) {
  if (state & CTRL_DISABLED) return get_sys_color(brTextDisabled);
  if (part == THEME_PART_BUTTON && (state & CTRL_DEFAULT)) return 0xffffffff;
  if (part == THEME_PART_MENU_ITEM && (state & (CTRL_HOVER | CTRL_SELECTED | CTRL_PRESSED)))
    return get_sys_color(brControlBg);
  return get_sys_color(brTextNormal);
}

static void modern_draw_button_label(irect16_t r, const char *text, ctrl_state_t state) {
  draw_text_small(text, r.x, r.y, modern_foreground(THEME_PART_BUTTON, state));
}

static void modern_draw_combobox(irect16_t r, const char *text, ctrl_state_t state) {
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  modern_draw_combobox_bg(r, state);
  irect16_t arrow = rect_split_right(r, MIN(r.h, 16));
  irect16_t label = rect_inset_xy(rect_trim_right(r, arrow.w), 2, 0);
  uint32_t foreground = modern_foreground(THEME_PART_COMBOBOX, state);
  draw_text_clipped(FONT_SYSTEM, text, &label, foreground, TEXT_PADDING_LEFT);
  draw_theme_icon_in_rect(THEME_ICON_ARROW_UPDOWN, arrow, foreground);
}

static theme_t g_modern_theme = {
  .style                  = THEME_MODERN,
  .name                   = "Modern",
  .draw_button_label      = modern_draw_button_label,
  .draw_combobox          = modern_draw_combobox,
  .foreground             = modern_foreground,
  .draw_part              = modern_draw_part,
  .draw_window_chrome     = modern_draw_window_chrome,
  .draw_statusbar         = modern_draw_statusbar,
  .scrollbar_width        = 0,
  .scrollbar_overlay      = true,
  .press_icon_offset      = 0,
  .button_corner_radius   = RADIUS_BUTTON,
  .control_padding        = BUTTON_PADDING,
  .apply_palette          = modern_apply_palette,
};

theme_t *theme_modern_instance(void) { return &g_modern_theme; }
