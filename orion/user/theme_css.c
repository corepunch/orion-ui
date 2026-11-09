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
#include "orion/themes/base_css.h"

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

// A C theme painting CSS parts: its colour roles and its own radius metrics.
static bool theme_c_var(const char *name, char *out, size_t n, void *ctx) {
  if (theme_role_var(name, out, n, ctx)) return true;
  const theme_t *t = get_theme();
  static const struct { const char *name; size_t offset; } radii[] = {
    {"window-radius", offsetof(theme_t, window.corner_radius)},  {"button-radius", offsetof(theme_t, button.corner_radius)},
    {"field-radius", offsetof(theme_t, field.corner_radius)},    {"multiline-radius", offsetof(theme_t, field.multiline_corner_radius)},
    {"tab-radius", offsetof(theme_t, tab.corner_radius)},        {"toolbar-radius", offsetof(theme_t, toolbar.corner_radius)},
    {"table-radius", offsetof(theme_t, table.corner_radius)},    {"card-radius", offsetof(theme_t, card.corner_radius)},
    {"badge-radius", offsetof(theme_t, badge.corner_radius)},
  };
  for (size_t i = 0; i < sizeof(radii) / sizeof(radii[0]); i++) {
    if (strcmp(name, radii[i].name)) continue;
    int v = *(const int *)((const char *)t + radii[i].offset);
    snprintf(out, n, "%dpx", v == CORNER_RADIUS_CIRCULAR ? 9999 : v);
    return true;
  }
  return false;
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
    {"control-padding", BUTTON_PADDING},
  };
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
    if (!strcmp(name, k[i].name)) { snprintf(out, n, "%dpx", k[i].value); return true; }
  return false;
}

static char *theme_read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "[theme] cannot read ORION_THEME_CSS=%s\n", path); fflush(stderr); return NULL; }
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
  size_t got = text ? fread(text, 1, (size_t)size, f) : 0;
  fclose(f);
  if (text) text[got] = 0;
  return text;
}

style_sheet_t *theme_stylesheet(theme_t *theme) {
  if (!theme) return NULL;
  if (theme->sheet) return theme->sheet;
  if (!theme->css) {   // a C theme painting CSS parts: base.css over its palette
    static style_sheet_t *base[2];
    bool dark = theme->appearance.dark;
    if (!base[dark] && (base[dark] = style_sheet_parse(k_base_css, "orion/themes/base.css"))) {
      style_sheet_set_dark(base[dark], dark);
      style_sheet_set_var_hook(base[dark], theme_c_var, theme_platform_var, NULL);
    }
    return base[dark];
  }
  style_sheet_t *sheet = style_sheet_parse(k_base_css, "orion/themes/base.css");
  if (!sheet) return NULL;
  const char *path = getenv("ORION_THEME_CSS");
  char *override = path && *path ? theme_read_file(path) : NULL;
  style_sheet_append(sheet, override ? override : theme->css, override ? path : theme->css_origin);
  free(override);
  style_sheet_set_dark(sheet, theme->appearance.dark);
  style_sheet_set_var_hook(sheet, theme_role_var, theme_platform_var, NULL);
  theme->sheet = sheet;
  return sheet;
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

// ── metrics ───────────────────────────────────────────────────────────────────

static style_query_t q_elem(const char *element, const char *part) {
  return (style_query_t){ .element = element, .part = part };
}

// border-radius of 9999px and more is a capsule, as CSS draws pills.
// The top-left corner of border-radius (tabs round only their top corners).
static void read_radius(const style_sheet_t *s, style_query_t q, int *out) {
  char v[128];
  if (!style_value(s, q, "border-radius", v, sizeof(v))) return;
  v[strcspn(v, " \t")] = 0;
  int r;
  if (style_parse_length(v, &r)) *out = r >= 9999 ? CORNER_RADIUS_CIRCULAR : r;
  else { fprintf(stderr, "[theme] border-radius: \"%s\" is not a px length\n", v); fflush(stderr); }
}

// The first length of a 1..4 value property.
static void read_first(const style_sheet_t *s, style_query_t q, const char *property, int *out) {
  char v[128];
  if (!style_value(s, q, property, v, sizeof(v))) return;
  v[strcspn(v, " \t")] = 0;
  if (!style_parse_length(v, out)) { fprintf(stderr, "[theme] %s: \"%s\" is not a px length\n", property, v); fflush(stderr); }
}

// 1..4 lengths in CSS order (top, right, bottom, left).
static bool theme_four(const char *value, int out[4]) {
  char buf[128], *tok[4]; int n = 0;
  snprintf(buf, sizeof(buf), "%s", value);
  for (char *c = strtok(buf, " \t"); c && n < 4; c = strtok(NULL, " \t")) tok[n++] = c;
  int v[4];
  for (int i = 0; i < n; i++) if (!style_parse_length(tok[i], &v[i])) return false;
  if (!n) return false;
  static const int map[5][4] = {{0}, {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
  for (int i = 0; i < 4; i++) out[i] = v[map[n][i]];
  return true;
}

// A box-shadow of dx dy blur colour.
static void theme_shadow(const char *value, ipoint16_t *offset, int *blur, uint32_t *color) {
  char buf[128], *tok[5]; int n = 0, len[3], nl = 0;
  snprintf(buf, sizeof(buf), "%s", value);
  for (char *c = strtok(buf, " \t"); c && n < 5; c = strtok(NULL, " \t")) tok[n++] = c;
  for (int i = 0; i < n; i++) {
    if (nl < 3 && style_parse_length(tok[i], &len[nl])) nl++;
    else if (!style_parse_color(tok[i], color)) { fprintf(stderr, "[theme] box-shadow: bad \"%s\"\n", value); fflush(stderr); return; }
  }
  if (nl >= 2) { offset->x = (int16_t)len[0]; offset->y = (int16_t)len[1]; }
  if (nl >= 3) *blur = len[2];
}

static void read_role(const style_sheet_t *s, style_query_t q, const char *property, int *out) {
  char name[64];
  if (!style_var_name(s, q, property, name, sizeof(name))) return;
  int role = theme_sys_color_role(name);
  if (role >= 0) *out = role;
  else { fprintf(stderr, "[theme] %s: --%s is not a system colour role\n", property, name); fflush(stderr); }
}

static void read_fraction(const style_sheet_t *s, style_query_t q, const char *property, int *out) {
  float f;
  if (style_number(s, q, property, &f)) *out = (int)(f * 255.0f + 0.5f);
}

void theme_metrics_from_stylesheet(theme_t *t, const style_sheet_t *s) {
  if (!t || !s) return;
  style_query_t multiline = { .element = "TextEdit", .classes = "multiline" };
  read_radius(s, q_elem("Window", NULL), &t->window.corner_radius);
  read_radius(s, q_elem("Button", NULL), &t->button.corner_radius);
  read_radius(s, q_elem("TextEdit", NULL), &t->field.corner_radius);
  read_radius(s, multiline, &t->field.multiline_corner_radius);
  read_radius(s, q_elem("TabView", "tab"), &t->tab.corner_radius);
  read_radius(s, q_elem("Toolbar", "button"), &t->toolbar.corner_radius);
  style_length(s, q_elem("Toolbar", NULL), "gap", &t->toolbar.spacing);

  read_radius(s, q_elem("Card", NULL), &t->card.corner_radius);
  read_role(s, q_elem("Card", NULL), "background-color", &t->card.face);
  read_role(s, q_elem("Card", NULL), "border-color", &t->card.outline);
  style_length(s, q_elem("Card", NULL), "--highlight-width", &t->card.highlight_width);
  char material[16];
  if (style_value(s, q_elem("Card", NULL), "--plastic", material, sizeof(material))) t->plastic.flat = !strcmp(material, "flat");

  style_query_t chosen_tile = { .element = "IconView", .part = "thumbnail", .state = STYLE_CHECKED };
  style_length(s, chosen_tile, "border-width", &t->thumbnail.ring_width);
  read_radius(s, q_elem("TableView", NULL), &t->table.corner_radius);
  style_length(s, q_elem("TableView", "header"), "min-height", &t->report.header_height);
  style_length(s, q_elem("TableView", "row"), "min-height", &t->report.row_height);
  int divider = t->report.row_dividers ? 1 : 0;
  if (style_length(s, q_elem("TableView", "row"), "--divider-width", &divider)) t->report.row_dividers = divider > 0;

  // Chrome geometry: the default sheet sets it, as a browser's user-agent sheet does.
  style_length(s, q_elem("Window", "titlebar"), "height", &t->caption.height);
  style_length(s, q_elem("MenuBar", NULL), "height", &t->menubar.height);
  style_length(s, q_elem("Scrollbar", NULL), "width", &t->scrollbar.width);
  style_length(s, q_elem("Toolbar", "button"), "height", &t->toolbar.button_size);
  read_first(s, q_elem("Toolbar", NULL), "padding", &t->toolbar.padding);
  style_length(s, q_elem("TabView", "tab"), "height", &t->tab.header.height);
  style_length(s, q_elem("TabView", "tab"), "min-width", &t->tab.min_width);
  style_length(s, q_elem("TabView", "tab"), "padding-left", &t->tab.padding);
  style_length(s, q_elem("TabView", "pane"), "--frame-width", &t->tab.pane_frame);
  style_length(s, q_elem("TabView", "pane"), "--pane-margin", &t->tab.pane_margin);
  style_length(s, q_elem("SplitView", NULL), "gap", &t->splitter.spacing);
  style_length(s, q_elem("Button", NULL), "padding-left", &t->control.padding);
  style_length(s, q_elem("Button", NULL), "--press-offset", &t->control.press_icon_offset);
  char v[128];
  if (style_value(s, q_elem("Button", NULL), "text-shadow", v, sizeof(v))) t->text_shadow = strcmp(v, "none") != 0;
  if (style_value(s, q_elem("Menu", "item"), "--shape", v, sizeof(v))) t->menu.capsule = !strcmp(v, "capsule");
  int pad[4];
  if (style_value(s, q_elem("Card", NULL), "padding", v, sizeof(v)) && theme_four(v, pad)) {
    t->card.padding_y = pad[0]; t->card.padding_x = pad[1];
  }
  if (style_value(s, q_elem("Window", NULL), "box-shadow", v, sizeof(v))) {
    memset(&t->window.shadow, 0, sizeof(t->window.shadow));
    if (strcmp(v, "none")) theme_shadow(v, &t->window.shadow.offset, &t->window.shadow.blur, &t->window.shadow.color);
  }

  read_radius(s, q_elem("Badge", NULL), &t->badge.corner_radius);
  style_length(s, q_elem("Badge", NULL), "padding-left", &t->badge.padding);
  read_fraction(s, q_elem("Badge", NULL), "--tint-alpha", &t->badge.tint_alpha);
  read_fraction(s, q_elem("Badge", NULL), "--ink-mix", &t->badge.ink_mix);
  style_length(s, q_elem("Badge", "icon"), "width", &t->badge.icon_size);
  style_length(s, q_elem("Badge", "icon"), "margin-right", &t->badge.icon_gap);
}

void theme_init_from_stylesheet(theme_t *theme) {
  style_sheet_t *sheet = theme_stylesheet(theme);
  if (!sheet) return;
  theme_metrics_from_stylesheet(theme, sheet);
  theme->field.padding_x = theme->field.corner_radius == CORNER_RADIUS_CIRCULAR
                         ? TEXTEDIT_PADDING_CAPSULE : FIELD_PADDING_X(theme->field.corner_radius);
}
