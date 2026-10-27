// System color theme table and active-theme runtime.
// Analogous to WinAPI GetSysColor / SetSysColors.
// Access colours via get_sys_color(brXxx).
// Theme switches update g_sys_colors via theme_t.apply_palette() inside
// set_theme(); set_sys_colors() is for runtime overrides on top of the
// active theme palette.

#include <stdint.h>
#include <stdio.h>
#include "messages.h"
#include "user.h"
#include "theme.h"
#include "theme_palette_dark.h"

uint32_t g_sys_colors[brCount] = { THEME_PALETTE_DARK_INIT };

// ── Active theme runtime ───────────────────────────────────────────────────

static theme_t *g_active_theme = NULL;

static theme_t *theme_for_style(theme_style_t style) {
  switch (style) {
    case THEME_CLASSIC: return theme_classic_instance();
    case THEME_MODERN:  return theme_modern_instance();
    case THEME_LIGHT:   return theme_light_instance();
    case THEME_NAVY:    return theme_navy_instance();
    case THEME_AMBER:   return theme_amber_instance();
    case THEME_PORCELAIN: return theme_porcelain_instance();
    case THEME_PORCELAIN_DARK: return theme_porcelain_dark_instance();
    default:            return NULL;
  }
}

theme_t *get_theme(void) {
  if (!g_active_theme) {
    g_active_theme = theme_modern_instance();
    g_active_theme->apply_palette();
  }
  return g_active_theme;
}

// Returns true if all required vtable slots in t are non-NULL.
static bool theme_validate(theme_t *t) {
  if (!t) { fprintf(stderr, "[theme] NULL theme pointer\n"); fflush(stderr); return false; }
#define REQUIRE(fn) \
  if (!t->fn) { fprintf(stderr, "theme[%s]: missing " #fn "\n", t->name ? t->name : "?"); fflush(stderr); return false; }
  REQUIRE(draw_part)
  REQUIRE(draw_window_chrome)
  REQUIRE(draw_statusbar_text)
  REQUIRE(foreground)
  REQUIRE(draw_button_label)
  REQUIRE(draw_combobox)
  REQUIRE(apply_palette)
#undef REQUIRE
  if (t->scrollbar.width < 0 || (!t->scrollbar.overlay && t->scrollbar.width == 0) ||
      (t->scrollbar.overlay && t->scrollbar.width != 0) || t->control.padding < 0 ||
      t->control.press_icon_offset < 0 || t->window.corner_radius < 0 ||
      (t->button.corner_radius < 0 && t->button.corner_radius != CORNER_RADIUS_CIRCULAR) ||
      (t->field.corner_radius < 0 && t->field.corner_radius != CORNER_RADIUS_CIRCULAR) || t->field.padding_x <= 0 ||
      t->caption.height <= 0 || t->menubar.height <= 0 || t->toolbar.button_size <= 0 ||
      t->toolbar.padding < 0 || t->toolbar.spacing < 0 || t->toolbar.corner_radius < 0 || t->tab.corner_radius < 0 || t->tab.pane_margin < 0 || t->splitter.spacing < 0) {
    fprintf(stderr, "[theme] invalid metrics name=%s gutter=%d overlay=%d padding=%d offset=%d radius=%d window_radius=%d caption=%d menu=%d toolbar=%d toolbar.padding=%d tab.radius=%d pane.margin=%d splitter.spacing=%d\n",
            t->name, t->scrollbar.width, t->scrollbar.overlay, t->control.padding,
            t->control.press_icon_offset, t->button.corner_radius, t->window.corner_radius,
            t->caption.height, t->menubar.height, t->toolbar.button_size, t->toolbar.padding, t->tab.corner_radius, t->tab.pane_margin, t->splitter.spacing);
    fflush(stderr);
    return false;
  }
  if (t->speechbubble.corner_radius < 0 || t->speechbubble.tail_width <= 0 ||
      t->speechbubble.tail_length <= 0 || t->speechbubble.shadow.size < 0 || t->speechbubble.shadow.blur < 0) {
    fprintf(stderr, "[theme] invalid speech bubble metrics name=%s radius=%d width=%d length=%d shadow=%d blur=%d\n",
            t->name, t->speechbubble.corner_radius, t->speechbubble.tail_width,
            t->speechbubble.tail_length, t->speechbubble.shadow.size, t->speechbubble.shadow.blur);
    fflush(stderr);
    return false;
  }
  if (t->report.divider_width <= 0) {
    fprintf(stderr, "[theme] invalid report metrics name=%s divider_width=%d\n", t->name, t->report.divider_width);
    fflush(stderr);
    return false;
  }
  if (t->command_link.bullet_size <= 0 || t->command_link.gap < 0 || t->command_link.padding_y < 0) {
    fprintf(stderr, "[theme] invalid command link metrics name=%s bullet=%d gap=%d padding_y=%d\n",
            t->name, t->command_link.bullet_size, t->command_link.gap, t->command_link.padding_y);
    fflush(stderr);
    return false;
  }
  return true;
}

// Depth-first walk over the entire window tree, posting msg to every live
// window.  Covers regular children and toolbar-band embedded controls
// (toolbar_state_t->children), which live in a separate list from win->children.
static void post_to_win_tree(window_t *win, uint32_t msg, uint32_t wparam) {
  for (; win; win = win->next) {
    post_message(win, msg, wparam, NULL);
    post_to_win_tree(win->children, msg, wparam);
    toolbar_state_t *tb = window_toolbar_state(win);
    if (tb) post_to_win_tree(tb->children, msg, wparam);
  }
}

bool set_theme(theme_style_t style) {
  static bool s_switching = false;
  if (s_switching) {
    fprintf(stderr, "[theme] set_theme REJECTED re-entrant style=%d\n", (int)style);
    fflush(stderr);
    return false;
  }

  theme_t *candidate = theme_for_style(style);
  if (!candidate) {
    fprintf(stderr, "[theme] set_theme REJECTED invalid style=%d\n", (int)style);
    fflush(stderr);
    return false;
  }

  if (!theme_validate(candidate)) {
    fprintf(stderr, "[theme] set_theme REJECTED validation-failure style=%d\n", (int)style);
    fflush(stderr);
    return false;  // leave current theme active
  }

  if (g_active_theme && g_active_theme->style == style) {
    return false;
  }

  // Capture/drag safety: a live drag would leave the dragged control in a
  // stale visual state after the palette changes.  Synthesise a cancel by
  // delivering evMouseLeave to the capturer and then releasing capture, so
  // the next mouse-move or button-up arrives with no ghost pressed state.
  if (g_ui_runtime.captured) {
    window_t *capturer = g_ui_runtime.captured;
    set_capture(NULL);
    post_message(capturer, evMouseLeave, 0, NULL);
  }

  // Track geometry that changes client layout after the switch.
  int old_scrollbar_width = g_active_theme ? g_active_theme->scrollbar.width : 0;
  int old_caption_height = g_active_theme ? g_active_theme->caption.height : 0;
  int old_menubar_height = g_active_theme ? g_active_theme->menubar.height : 0;
  int old_toolbar_size = g_active_theme ? g_active_theme->toolbar.button_size : 0;
  int old_toolbar_padding = g_active_theme ? g_active_theme->toolbar.padding : 0;

  s_switching = true;
  g_active_theme = candidate;
  candidate->apply_palette();

  if (g_ui_runtime.running) {
    post_to_win_tree(g_ui_runtime.windows, evThemeChanged, (uint32_t)style);

    // Repaint only visible top-level windows; hidden windows receive the
    // evThemeChanged notification above so controls can update caches, but
    // should not trigger a redundant paint until they become visible.
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (window_has_state(w, WINDOW_STATE_VISIBLE)) invalidate_window(w);
    }

    // If the scrollbar gutter width changed the client area of every window
    // shrinks or grows; post evResize to all top-level windows so layout
    // managers recalculate scroll-channel allocations.
    if (candidate->scrollbar.width != old_scrollbar_width ||
        candidate->caption.height != old_caption_height ||
        candidate->menubar.height != old_menubar_height ||
        candidate->toolbar.button_size != old_toolbar_size ||
        candidate->toolbar.padding != old_toolbar_padding) {
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        post_message(w, evResize, 0, NULL);
      }
    }
  }
  s_switching = false;
  return true;
}

void set_sys_colors(int count, const int *indices, const uint32_t *colors) {
  for (int i = 0; i < count; i++) {
    if (indices[i] >= 0 && indices[i] < brCount) {
      g_sys_colors[indices[i]] = colors[i];
    }
  }
  if (g_ui_runtime.running) {
    post_message((window_t*)1, evRefreshStencil, 0, NULL);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (window_has_state(w, WINDOW_STATE_VISIBLE)) invalidate_window(w);
    }
  }
}

void theme_draw(theme_part_t part, irect16_t r, ctrl_state_t state) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    fprintf(stderr, "[theme] draw REJECTED part=%d state=%u rect=%d,%d,%d,%d\n",
            (int)part, (unsigned)state, r.x, r.y, r.w, r.h);
    fflush(stderr);
    return;
  }
  if (r.w <= 0 || r.h <= 0) return;
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  get_theme()->draw_part(part, r, state);
}

static bool theme_part_is_plastic(theme_part_t part) {
  return part == THEME_PART_BUTTON || part == THEME_PART_CARD ||
         part == THEME_PART_TOOLBAR_BUTTON || part == THEME_PART_TOOLBAR_LABELED_BUTTON;
}

void theme_draw_ex(theme_part_t part, irect16_t r, ctrl_state_t state, const theme_draw_opts_t *opts) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    fprintf(stderr, "[theme] draw_ex REJECTED part=%d state=%u rect=%d,%d,%d,%d\n",
            (int)part, (unsigned)state, r.x, r.y, r.w, r.h);
    fflush(stderr);
    return;
  }
  if (r.w <= 0 || r.h <= 0) return;
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  theme_draw_opts_t none = {0};
  if (!opts) opts = &none;
  bool plastic = (state & CTRL_PLASTIC) && theme_part_is_plastic(part);
  state &= ~CTRL_PLASTIC;
  if (part == THEME_PART_CARD) {
    if (plastic) draw_plastic_card(r, state, opts->color);
    else get_theme()->draw_card(r, state, opts->edge_color);
  } else if (plastic) {
    draw_plastic_button(r, state, opts->color, opts->icon, opts->control_size, opts->round);
  } else {
    get_theme()->draw_part(part, r, state);
  }
}

irect16_t theme_content_rect(theme_part_t part, irect16_t r, ctrl_state_t state) {
  const theme_t *theme = get_theme();
  if (part != THEME_PART_CARD) return r;
  int ring = theme->card.ring_width;
  if (state & CTRL_PLASTIC) ring += theme->card.shadow_size;
  return rect_inset(r, ring);
}

int theme_control_extent(int regular, uint32_t style) {
  int pct = get_theme()->control.size_pct[CONTROL_SIZE_INDEX(style)];
  if (pct <= 0) pct = 100;
  return MAX(1, (regular * pct + 50) / 100);
}

uint32_t theme_foreground(theme_part_t part, ctrl_state_t state) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    fprintf(stderr, "[theme] foreground REJECTED part=%d state=%u\n", (int)part, (unsigned)state);
    fflush(stderr);
    return get_sys_color(brTextNormal);
  }
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  return get_theme()->foreground(part, state);
}

// ── Appearance ──────────────────────────────────────────────────────────────

static appearance_t g_appearance = APPEARANCE_SYSTEM;
static int g_system_dark = -1;  // unknown until the window system reports it

// Switch to the active theme's counterpart when its appearance differs from the wanted one.
static void theme_apply_appearance(void) {
  if (g_appearance == APPEARANCE_SYSTEM && g_system_dark < 0) return;
  bool dark = g_appearance == APPEARANCE_SYSTEM ? g_system_dark != 0 : g_appearance == APPEARANCE_DARK;
  theme_t *theme = get_theme();
  if (theme->appearance.dark == dark || theme->appearance.counterpart == theme->style) return;
  theme_t *other = theme_for_style(theme->appearance.counterpart);
  if (!other || other->appearance.dark != dark) {
    fprintf(stderr, "[theme] appearance rejected theme=%s counterpart=%d dark=%d\n",
            theme->name, (int)theme->appearance.counterpart, (int)dark);
    fflush(stderr);
    return;
  }
  set_theme(other->style);
}

void set_appearance(appearance_t appearance) {
  if (appearance != APPEARANCE_SYSTEM && appearance != APPEARANCE_LIGHT && appearance != APPEARANCE_DARK) {
    fprintf(stderr, "[theme] set_appearance rejected appearance=%d\n", (int)appearance);
    fflush(stderr);
    return;
  }
  g_appearance = appearance;
  theme_apply_appearance();
}

appearance_t get_appearance(void) { return g_appearance; }

void theme_system_appearance_changed(bool dark) {
  g_system_dark = dark;
  if (g_appearance == APPEARANCE_SYSTEM) theme_apply_appearance();
}
