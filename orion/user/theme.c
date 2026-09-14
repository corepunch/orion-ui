// System color theme table and active-theme runtime.
// Analogous to WinAPI GetSysColor / SetSysColors.
// Access colours via get_sys_color(brXxx); change them via set_sys_colors().

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
  [brTransparent]          = 0x00000000,   // fully transparent
  [brControlBg]            = 0xff3c3c3c,   // dialog, panel, and control face
  [brWindowDarkBg]         = 0xff2c2c2c,   // dark secondary panel background
  [brWorkspaceBg]          = 0xff1e1e1e,   // darker than status bar — canvas workspace
  [brActiveTitlebar]       = 0xffa05a1e,   // focused window: blue caption bar (dark theme)
  [brActiveTitlebarText]   = 0xffffffff,   // focused caption text: white
  [brInactiveTitlebar]     = 0xff2c2c2c,   // unfocused: flat dark gray
  [brInactiveTitlebarText] = 0xff787878,   // unfocused caption text: medium gray
  [brStatusbarBg]          = 0xff2c2c2c,   // status bar background
  [brLightEdge]            = 0xff7f7f7f,   // top-left edge for beveled elements
  [brDarkEdge]             = 0xff1a1a1a,   // bottom-right edge for bevel
  [brFlare]                = 0xffcfcfcf,   // corner flare for beveled elements
  [brAccent]               = 0xff5EC4F3,   // focus, selection, and active-state accent
  [brButtonInner]          = 0xff505050,   // inner fill of button
  [brButtonHover]          = 0xff5a5a5a,   // slightly brighter for hover state
  [brTextNormal]           = 0xffc0c0c0,   // standard text color
  [brTextDisabled]         = 0xff808080,   // for disabled/inactive text
  [brTextError]            = 0xffff4444,   // red text for errors
  [brTextSuccess]          = 0xff44ff44,   // green text for success messages
  [brBorderFocus]          = 0xff101010,   // very dark outline for focused item
  [brBorderActive]         = 0xff808080,   // light gray for active border
  [brFolderText]           = 0xffa0d000,   // folder entry text in file lists
  [brColumnViewBg]         = 0xff544e47,   // blue-gray for report/icon column views
  [brModalOverlay]         = 0x40402000,   // modal owner dim overlay (semi-transparent)
  [brToolbarForeground]    = 0xffd8d8d8,   // neutral light gray for toolbar content
};

// ── Active theme runtime ───────────────────────────────────────────────────

static theme_t *g_active_theme = NULL;

theme_t *get_theme(void) {
  if (!g_active_theme) g_active_theme = theme_modern_instance();
  return g_active_theme;
}

// Returns true if all required vtable slots in t are non-NULL.
static bool theme_validate(theme_t *t) {
  if (!t) { fprintf(stderr, "theme: NULL theme pointer\n"); return false; }
#define REQUIRE(fn) \
  if (!t->fn) { fprintf(stderr, "theme[%s]: missing " #fn "\n", t->name ? t->name : "?"); return false; }
  REQUIRE(draw_bevel)
  REQUIRE(draw_button_bg)
  REQUIRE(draw_toolbar_item_bg)
  REQUIRE(draw_toolbar_separator)
  REQUIRE(draw_panel_bg)
  REQUIRE(draw_titlebar_bg)
  REQUIRE(draw_statusbar_bg)
  REQUIRE(draw_checkbox_box)
  REQUIRE(apply_palette)
#undef REQUIRE
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

  s_switching = true;
  g_active_theme = candidate;
  candidate->apply_palette();
  THEME_TRACE("palette applied name=%s", candidate->name);

  if (g_ui_runtime.running) {
    THEME_TRACE("broadcasting evThemeChanged style=%d name=%s",
                (int)style, candidate->name);
    post_to_win_tree(g_ui_runtime.windows, evThemeChanged, (uint32_t)style);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (window_has_state(w, WINDOW_STATE_VISIBLE)) invalidate_window(w);
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
