// Themes from style sheets: the system colour roles are :root custom
// properties, theme_t's metrics are properties of controls and parts, and
// theme_box.c paints every part from its box properties. docs/styling.md
// lists the names.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "messages.h"
#include "theme.h"
#include "style.h"
#include "config.h"

static const char *const k_sys_color_names[brCount] = {
  [brTransparent] = "transparent",                 [brControlBg] = "control-bg",
  [brWindowDarkBg] = "window-dark-bg",             [brWorkspaceBg] = "workspace-bg",
  [brActiveTitlebar] = "active-titlebar",          [brActiveTitlebarText] = "active-titlebar-text",
  [brInactiveTitlebar] = "inactive-titlebar",      [brInactiveTitlebarText] = "inactive-titlebar-text",
  [brStatusbarBg] = "statusbar-bg",                [brLightEdge] = "light-edge",
  [brDarkEdge] = "dark-edge",                      [brFlare] = "flare",
  [brAccent] = "accent",                           [brButtonInner] = "button-inner",
  [brButtonHover] = "button-hover",                [brTextNormal] = "text-normal",
  [brTextDisabled] = "text-disabled",              [brTextError] = "text-error",
  [brTextSuccess] = "text-success",                [brBorderFocus] = "border-focus",
  [brBorderActive] = "border-active",              [brFolderText] = "folder-text",
  [brColumnViewBg] = "column-view-bg",             [brModalOverlay] = "modal-overlay",
  [brToolbarForeground] = "toolbar-foreground",    [brPanelDark] = "panel-dark",
  [brPanelDarker] = "panel-darker",                [brTextWarning] = "text-warning",
  [brTextInfo] = "text-info",                      [brTextSecondary] = "text-secondary",
  [brTextOnColor] = "text-on-color",               [brSelectionTop] = "selection-top",
  [brSelectionBottom] = "selection-bottom",        [brTabBarBg] = "tab-bar-bg",
  [brAccentText] = "accent-text",                  [brDesktopBg] = "desktop-bg",
  [brInfoBackground] = "info-background",          [brInfoText] = "info-text",
  [brTooltipBackground] = "tooltip-background",    [brTooltipText] = "tooltip-text",
};

const char *theme_sys_color_name(int role) {
  return role >= 0 && role < brCount ? k_sys_color_names[role] : NULL;
}

int theme_sys_color_role(const char *name) {
  for (int i = 0; name && i < brCount; i++)
    if (k_sys_color_names[i] && !strcmp(k_sys_color_names[i], name)) return i;
  return -1;
}

// ── sheets ────────────────────────────────────────────────────────────────────

// var(--accent) while painting: the live palette, so set_sys_colors() overrides apply.
static bool theme_role_var(const char *name, char *out, size_t n, void *ctx) {
  (void)ctx;
  int role = theme_sys_color_role(name);
  if (role < 0) return false;
  uint32_t c = g_sys_colors[role];
  snprintf(out, n, "#%02X%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24);
  return true;
}

#if defined(__APPLE__) && TARGET_OS_IOS
#define THEME_MENUBAR_HEIGHT 29
#else
#define THEME_MENUBAR_HEIGHT (FONT_SIZE + 12)
#endif

// Platform metrics a sheet may use without defining them, like a browser's
// device values: sizes differ between desktop and touch builds.
static bool theme_platform_var(const char *name, char *out, size_t n, void *ctx) {
  (void)ctx;
  static const struct { const char *name; int value; } k[] = {
    {"control-radius", (CONTROL_HEIGHT_REGULAR + 1) / 2}, {"control-height", CONTROL_HEIGHT_REGULAR},
    {"titlebar-height", TITLEBAR_HEIGHT},                 {"menubar-height", THEME_MENUBAR_HEIGHT},
    {"toolbar-height", TB_SPACING},                       {"icon-size", SYSICON_SIZE},
    {"font-size", FONT_SIZE},                             {"scrollbar-width", SCROLLBAR_WIDTH},
    {"control-padding", BUTTON_PADDING},                  {"compact-icon-size", TOOLBAR_COMPACT_ICON_SIZE},
  };
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if (!strcmp(name, k[i].name)) { snprintf(out, n, "%dpx", k[i].value); return true; }
  return false;
}

static char *theme_read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
  size_t got = text ? fread(text, 1, (size_t)size, f) : 0;
  fclose(f);
  if (text) text[got] = 0;
  return text;
}

static char *theme_read_share(const char *origin) {
  char path[512];
  snprintf(path, sizeof(path), "%s/../share/%s", ui_get_exe_dir(), origin);
  char *css = theme_read_file(path);
  if (!css) fprintf(stderr, "[theme] cannot read %s\n", path);
  return css;
}

static char *g_app_css, *g_app_origin;   // ui_set_app_stylesheet()

// base.css, the theme's sheet, then an application's.
static style_sheet_t *theme_build_sheet(theme_t *theme, const char *app_css, const char *app_origin) {
  char *base = theme_read_share("orion/themes/base.css");
  if (!base) return NULL;
  style_sheet_t *sheet = style_sheet_parse(base, "orion/themes/base.css");
  free(base);
  if (!sheet) return NULL;
  const char *override_path = getenv("ORION_THEME_CSS");
  if (override_path && *override_path) {
    char *override = theme_read_file(override_path);
    if (override) { style_sheet_append(sheet, override, override_path); free(override); }
  } else {
    char *theme_css = theme_read_share(theme->css_origin);
    if (!theme_css) { style_sheet_free(sheet); return NULL; }   // base.css alone has no palette
    style_sheet_append(sheet, theme_css, theme->css_origin); free(theme_css);
  }
  if (app_css) style_sheet_append(sheet, app_css, app_origin);
  style_sheet_set_dark(sheet, theme->appearance.dark);
  style_sheet_set_var_hook(sheet, theme_role_var, theme_platform_var, NULL);
  return sheet;
}

style_sheet_t *theme_stylesheet(theme_t *theme) {
  if (!theme || !theme->css_origin) return NULL;
  if (!theme->sheet) theme->sheet = theme_build_sheet(theme, g_app_css, g_app_origin);
  return theme->sheet;
}

// ── application instances' sheets ─────────────────────────────────────────────
//
// A gem shares the process with the shell and other gems, so its sheet belongs
// to its instance: windows with that hinstance read it (ui_selector), others
// do not. Instance 0 is the process: its sheet is the app sheet every query sees.

#define THEME_MAX_INSTANCE_SHEETS 32
static struct {
  hinstance_t hinstance;
  char *css, *origin;
  style_sheet_t *sheet;   // built for `theme`
  theme_t *theme;
} g_instance_sheets[THEME_MAX_INSTANCE_SHEETS];
static int g_instance_sheet_count;

void theme_reset_instance_stylesheets(void) {
  for (int i = 0; i < g_instance_sheet_count; i++) {
    style_sheet_free(g_instance_sheets[i].sheet);
    g_instance_sheets[i].sheet = NULL;
    g_instance_sheets[i].theme = NULL;
  }
}

style_sheet_t *ui_instance_stylesheet(hinstance_t hinstance) {
  if (!hinstance) return NULL;
  theme_t *theme = get_theme();
  for (int i = 0; i < g_instance_sheet_count; i++) {
    if (g_instance_sheets[i].hinstance != hinstance) continue;
    if (g_instance_sheets[i].theme != theme) {
      style_sheet_free(g_instance_sheets[i].sheet);
      g_instance_sheets[i].sheet = theme_build_sheet(theme, g_instance_sheets[i].css, g_instance_sheets[i].origin);
      g_instance_sheets[i].theme = theme;
    }
    return g_instance_sheets[i].sheet;
  }
  return NULL;
}

bool ui_has_instance_stylesheet(hinstance_t hinstance) {
  if (!hinstance) return g_app_css != NULL;
  for (int i = 0; i < g_instance_sheet_count; i++) if (g_instance_sheets[i].hinstance == hinstance) return true;
  return false;
}

// ── palette ───────────────────────────────────────────────────────────────────

bool theme_palette_from_stylesheet(style_sheet_t *sheet, uint32_t palette[brCount]) {
  if (!sheet || !palette) return false;
  style_sheet_set_var_hook(sheet, NULL, theme_platform_var, NULL);   // the sheet's own colours, not the live ones
  int missing = 0;
  for (int role = 0; role < brCount; role++) {
    palette[role] = 0;
    if (!k_sys_color_names[role] || role == brTransparent) continue;
    char prop[64]; snprintf(prop, sizeof(prop), "--%s", k_sys_color_names[role]);
    if (style_color(sheet, STYLE_ROOT, prop, &palette[role])) continue;
    fprintf(stderr, "[theme] style sheet does not define the colour %s\n", prop);
    missing++;
  }
  style_sheet_set_var_hook(sheet, theme_role_var, theme_platform_var, NULL);
  if (missing) fflush(stderr);
  return missing == 0;
}

void theme_css_apply_palette(void) {
  theme_t *theme = get_theme();
  uint32_t palette[brCount];
  if (theme_palette_from_stylesheet(theme_stylesheet(theme), palette)) theme_copy_palette(palette);
  else { fprintf(stderr, "[theme] %s: incomplete palette, keeping the previous colours\n", theme->name); fflush(stderr); }
}

// ── application sheet ─────────────────────────────────────────────────────────

void theme_reset_stylesheets(void);   // theme_builtin.c: drops the parsed sheets
void theme_refresh_windows(hinstance_t hinstance);

void ui_set_app_stylesheet(const char *css, const char *origin) {
  free(g_app_css); free(g_app_origin);
  g_app_css = css ? strdup(css) : NULL;
  g_app_origin = css ? strdup(origin ? origin : "app.css") : NULL;
  theme_reset_stylesheets();
  g_theme_style_generation++;
  theme_refresh_windows(0);
}

void ui_set_instance_stylesheet(hinstance_t hinstance, const char *css, const char *origin) {
  if (!hinstance) { ui_set_app_stylesheet(css, origin); return; }
  int i = 0;
  while (i < g_instance_sheet_count && g_instance_sheets[i].hinstance != hinstance) i++;
  if (i == g_instance_sheet_count) {
    if (!css) return;
    if (i == THEME_MAX_INSTANCE_SHEETS) {
      fprintf(stderr, "[theme] instance %u: more than %d instance style sheets; %s is not loaded\n",
              (unsigned)hinstance, THEME_MAX_INSTANCE_SHEETS, origin ? origin : "app.css");
      fflush(stderr);
      return;
    }
    g_instance_sheet_count++;
  }
  style_sheet_free(g_instance_sheets[i].sheet);
  free(g_instance_sheets[i].css); free(g_instance_sheets[i].origin);
  if (!css) {   // removed: the last record takes its place
    g_instance_sheets[i] = g_instance_sheets[--g_instance_sheet_count];
    memset(&g_instance_sheets[g_instance_sheet_count], 0, sizeof(g_instance_sheets[0]));
  } else {
    g_instance_sheets[i].hinstance = hinstance;
    g_instance_sheets[i].css = strdup(css);
    g_instance_sheets[i].origin = strdup(origin ? origin : "app.css");
    g_instance_sheets[i].sheet = NULL;
    g_instance_sheets[i].theme = NULL;
  }
  g_theme_style_generation++;
  theme_refresh_windows(hinstance);
}
