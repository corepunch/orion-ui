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

// Always-on trace for theme switch lifecycle and state transitions.
// One line per discrete event; keep noise-free so logs stay auditable.
#define THEME_TRACE(...) do { \
  fprintf(stderr, "[theme] " __VA_ARGS__); \
  fputc('\n', stderr); \
  fflush(stderr); \
} while (0)

uint32_t g_sys_colors[brCount] = {
  [brTransparent]          = 0x00000000,                 // fully transparent
  [brControlBg]            = WEB(0x17243B),              // main navy
  [brWindowDarkBg]         = WEB(0x1A2942),              // panel outer
  [brWorkspaceBg]          = WEB(0x17243B),              // canvas workspace
  [brActiveTitlebar]       = WEB(0x19263E),              // top chrome
  [brActiveTitlebarText]   = WEB(0xF6F8FF),              // text on dark
  [brInactiveTitlebar]     = WEB(0x17243B),              // unfocused caption
  [brInactiveTitlebarText] = WEB(0x647089),              // secondary text
  [brStatusbarBg]          = WEB(0x19273E),              // timeline / status
  [brLightEdge]            = WEB(0x4A5C7A),              // field/slider/button hairline
  [brDarkEdge]             = WEB(0x2A3954),              // quiet divider / bevel shadow
  [brFlare]                = WEB(0xF8FAFF),              // light card
  [brAccent]               = WEB(0x7357F6),              // primary purple
  [brButtonInner]          = WEB(0x1B2942),              // left toolbar
  [brButtonHover]          = WEB(0x243552),              // lifted navy hover
  [brTextNormal]           = WEB(0xF6F8FF),              // text on dark
  [brTextDisabled]         = WEB(0x647089),              // secondary / disabled
  [brTextError]            = WEB(0xC42B1C),              // error
  [brTextSuccess]          = WEB(0x63C994),              // success
  [brBorderFocus]          = WEB(0x8B70FF),              // purple border
  [brBorderActive]         = WEB(0x2A3954),              // dark UI border
  [brFolderText]           = WEB(0x4C91F5),              // cyan
  [brColumnViewBg]         = WEB(0x1A2942),              // panel outer
  [brModalOverlay]         = (WEB(0x17243B) & 0x00ffffffu) | 0x40000000u,
  [brToolbarForeground]    = WEB(0xF6F8FF),              // text on dark
};

// ── Active theme runtime ───────────────────────────────────────────────────

static theme_t *g_active_theme = NULL;

theme_t *get_theme(void) {
  if (!g_active_theme) {
    g_active_theme = theme_modern_instance();
    g_active_theme->apply_palette();
    THEME_TRACE("default theme applied name=%s", g_active_theme->name);
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
  if (t->scrollbar_width < 0 || (!t->scrollbar_overlay && t->scrollbar_width == 0) ||
      (t->scrollbar_overlay && t->scrollbar_width != 0) || t->control_padding < 0 ||
      t->press_icon_offset < 0 || t->button_corner_radius < 0 || t->window_corner_radius < 0) {
    THEME_TRACE("invalid metrics name=%s gutter=%d overlay=%d padding=%d offset=%d radius=%d window_radius=%d",
                t->name, t->scrollbar_width, t->scrollbar_overlay, t->control_padding,
                t->press_icon_offset, t->button_corner_radius, t->window_corner_radius);
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
    THEME_TRACE("set_theme REJECTED re-entrant style=%d", (int)style);
    return false;
  }

  if (style != THEME_CLASSIC && style != THEME_MODERN) {
    THEME_TRACE("set_theme REJECTED invalid style=%d", (int)style);
    return false;
  }
  theme_t *candidate = (style == THEME_MODERN)
                     ? theme_modern_instance()
                     : theme_classic_instance();

  THEME_TRACE("set_theme ENTER style=%d name=%s", (int)style,
              candidate && candidate->name ? candidate->name : "?");

  if (!theme_validate(candidate)) {
    THEME_TRACE("set_theme REJECTED validation-failure style=%d", (int)style);
    return false;  // leave current theme active
  }

  if (g_active_theme && g_active_theme->style == style) {
    THEME_TRACE("set_theme REJECTED same-theme name=%s", candidate->name);
    return false;
  }

  // Capture/drag safety: a live drag would leave the dragged control in a
  // stale visual state after the palette changes.  Synthesise a cancel by
  // delivering evMouseLeave to the capturer and then releasing capture, so
  // the next mouse-move or button-up arrives with no ghost pressed state.
  if (g_ui_runtime.captured) {
    THEME_TRACE("capture active during theme switch — cancelling capture on %p",
                (void *)g_ui_runtime.captured);
    window_t *capturer = g_ui_runtime.captured;
    set_capture(NULL);
    post_message(capturer, evMouseLeave, 0, NULL);
  }

  // Record the old scrollbar gutter width so we know whether layout needs
  // to be recalculated after the switch.
  int old_scrollbar_width = g_active_theme ? g_active_theme->scrollbar_width : 0;

  s_switching = true;
  g_active_theme = candidate;
  candidate->apply_palette();
  THEME_TRACE("palette applied name=%s", candidate->name);

  if (g_ui_runtime.running) {
    THEME_TRACE("broadcasting evThemeChanged style=%d name=%s",
                (int)style, candidate->name);
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
    if (candidate->scrollbar_width != old_scrollbar_width) {
      THEME_TRACE("scrollbar_width changed %d->%d, posting evResize to roots",
                  old_scrollbar_width, candidate->scrollbar_width);
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        post_message(w, evResize, 0, NULL);
      }
    }
  }
  s_switching = false;
  THEME_TRACE("set_theme DONE name=%s", candidate->name);
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
    THEME_TRACE("draw REJECTED part=%d state=%u rect=%d,%d,%d,%d",
                (int)part, (unsigned)state, r.x, r.y, r.w, r.h);
    return;
  }
  if (r.w <= 0 || r.h <= 0) return;
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  get_theme()->draw_part(part, r, state);
}

uint32_t theme_foreground(theme_part_t part, ctrl_state_t state) {
  if (part < 0 || part >= THEME_PART_COUNT) {
    THEME_TRACE("foreground REJECTED part=%d state=%u", (int)part, (unsigned)state);
    return get_sys_color(brTextNormal);
  }
  if (state & CTRL_DISABLED) state &= ~(CTRL_HOVER | CTRL_PRESSED);
  return get_theme()->foreground(part, state);
}
