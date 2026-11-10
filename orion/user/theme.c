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

uint32_t g_sys_colors[brCount];

// The palette is ready before any code reads a colour: the default theme's
// sheet is applied when libuser loads (a standalone ORION_THEME runs after).
__attribute__((constructor)) static void theme_initial_palette(void) { get_theme(); }

// ── Active theme runtime ───────────────────────────────────────────────────

static theme_t *g_active_theme = NULL;
unsigned g_theme_style_generation = 1;

static theme_t *theme_for_style(theme_style_t style) {
  switch (style) {
    case THEME_CLASSIC: return theme_classic_instance();
    case THEME_MODERN:  return theme_modern_instance();
    case THEME_LIGHT:   return theme_light_instance();
    case THEME_NAVY:    return theme_navy_instance();
    case THEME_AMBER:   return theme_amber_instance();
    case THEME_PORCELAIN: return theme_porcelain_instance();
    case THEME_PORCELAIN_DARK: return theme_porcelain_dark_instance();
    case THEME_METRO: return theme_metro_instance();
    default:            return NULL;
  }
}

theme_t *get_theme(void) {
  if (!g_active_theme) {
    g_active_theme = theme_modern_instance();
    theme_css_apply_palette();
    g_theme_style_generation++;
  }
  return g_active_theme;
}

// A theme is usable when its style sheet loads.
static bool theme_validate(theme_t *t) {
  if (!t) { fprintf(stderr, "[theme] NULL theme pointer\n"); fflush(stderr); return false; }
  if (!theme_stylesheet(t)) {   // its geometry and colours live there
    fprintf(stderr, "theme[%s]: style sheet failed to load\n", t->name ? t->name : "?"); fflush(stderr);
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

  s_switching = true;
  g_active_theme = candidate;
  g_theme_style_generation++;
  theme_css_apply_palette();
  g_theme_style_generation++;

  if (g_ui_runtime.running) {
    post_to_win_tree(g_ui_runtime.windows, evThemeChanged, (uint32_t)style);

    // Repaint only visible top-level windows; hidden windows receive the
    // evThemeChanged notification above so controls can update caches, but
    // should not trigger a redundant paint until they become visible.
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (window_has_state(w, WINDOW_STATE_VISIBLE)) invalidate_window(w);
    }

    // Geometry comes from the new sheet (scroll bar gutter, caption, toolbar,
    // fonts): every top-level window lays out again.
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) post_message(w, evResize, 0, NULL);
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
  g_theme_style_generation++;
  if (g_ui_runtime.running) {
    post_message((window_t*)1, evRefreshStencil, 0, NULL);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (window_has_state(w, WINDOW_STATE_VISIBLE)) invalidate_window(w);
    }
  }
}

static const theme_paint_recorder_t *g_paint_recorder;
void theme_set_paint_recorder(const theme_paint_recorder_t *recorder) { g_paint_recorder = recorder; }

// The window chrome, through the recorder when one is set.
void theme_paint_window_chrome(irect16_t titlebar, irect16_t caption, const char *title, ctrl_state_t state, bool maximizable) {
  if (g_paint_recorder && g_paint_recorder->chrome) { g_paint_recorder->chrome(titlebar, caption, title, state, maximizable); return; }
  theme_draw_window_chrome(titlebar, caption, title, state, maximizable);
}

void theme_draw(theme_part_t part, irect16_t r, ctrl_state_t state) { theme_draw_in(NULL, part, r, state); }

// A part of win's element: the window's classes, #id, instance variables and app sheet apply.
void theme_draw_in(const window_t *win, theme_part_t part, irect16_t r, ctrl_state_t state) {
  theme_draw_with(win, window_style_vars(win), part, r, state);
}

void theme_draw_with(const window_t *win, const ui_style_vars_t *vars, theme_part_t part, irect16_t r, ctrl_state_t state) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    fprintf(stderr, "[theme] draw REJECTED part=%d state=%u rect=%d,%d,%d,%d\n",
            (int)part, (unsigned)state, r.x, r.y, r.w, r.h);
    fflush(stderr);
    return;
  }
  if (r.w <= 0 || r.h <= 0) return;
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  if (g_paint_recorder && g_paint_recorder->part) { g_paint_recorder->part(part, r, state); return; }
  theme_css_draw_part_vars(part, r, state, ui_window_scope(win), vars);
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
  if (part == THEME_PART_CARD && (state & CTRL_PLASTIC)) {
    ui_style_vars_t tint = {0};
    ui_style_vars_set_color(&tint, "--surface", opts->color);
    theme_css_draw_part_vars(THEME_PART_CARD_PLASTIC, r, state & ~CTRL_PLASTIC, NULL, &tint);
  }
  else if (g_paint_recorder && g_paint_recorder->part) g_paint_recorder->part(part, r, state);
  else theme_css_draw_part(part, r, state, NULL);
}

// What a part leaves for content: its margin and border (Card::plastic's for a CTRL_PLASTIC card).
irect16_t theme_content_rect(theme_part_t part, irect16_t r, ctrl_state_t state) {
  if (part == THEME_PART_CARD && (state & CTRL_PLASTIC)) part = THEME_PART_CARD_PLASTIC;
  irect16_t in = theme_css_insets_vars(part, state & ~CTRL_PLASTIC, NULL, NULL);
  return R(r.x + in.x, r.y + in.y, r.w - in.x - in.w, r.h - in.y - in.h);
}

irect16_t theme_content_insets_in(const window_t *win, theme_part_t part, ctrl_state_t state) {
  return theme_css_insets_vars(part, state, ui_window_scope(win), window_style_vars(win));
}

uint32_t theme_foreground(theme_part_t part, ctrl_state_t state) { return theme_foreground_in(NULL, part, state); }

uint32_t theme_foreground_in(const window_t *win, theme_part_t part, ctrl_state_t state) {
  return theme_foreground_with(win, window_style_vars(win), part, state);
}

uint32_t theme_foreground_with(const window_t *win, const ui_style_vars_t *vars, theme_part_t part, ctrl_state_t state) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    fprintf(stderr, "[theme] foreground REJECTED part=%d state=%u\n", (int)part, (unsigned)state);
    fflush(stderr);
    return get_sys_color(brTextNormal);
  }
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  return theme_css_foreground_vars(part, state, ui_window_scope(win), vars);
}

uint32_t theme_fill_with(const window_t *win, const ui_style_vars_t *vars, theme_part_t part, ctrl_state_t state) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    fprintf(stderr, "[theme] fill REJECTED part=%d state=%u\n", (int)part, (unsigned)state);
    fflush(stderr);
    return get_sys_color(brTextNormal);
  }
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  return theme_css_fill_vars(part, state, ui_window_scope(win), vars);
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
