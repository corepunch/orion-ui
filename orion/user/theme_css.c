// Themes from style sheets: the system colour roles are :root custom
// properties, and theme_t's metrics are properties of controls and parts.
// docs/styling.md lists both.

#include <stdio.h>
#include <string.h>

#include "messages.h"
#include "theme.h"
#include "style.h"

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

bool theme_palette_from_stylesheet(const style_sheet_t *sheet, uint32_t palette[brCount]) {
  if (!sheet || !palette) return false;
  int missing = 0;
  for (int role = 0; role < brCount; role++) {
    palette[role] = 0;
    if (!k_sys_color_names[role] || role == brTransparent) continue;
    char prop[64]; snprintf(prop, sizeof(prop), "--%s", k_sys_color_names[role]);
    if (style_color(sheet, STYLE_ROOT, prop, &palette[role])) continue;
    fprintf(stderr, "[theme] style sheet does not define the colour %s\n", prop);
    missing++;
  }
  if (missing) fflush(stderr);
  return missing == 0;
}

static style_query_t q_elem(const char *element, const char *part) {
  return (style_query_t){ .element = element, .part = part };
}

// border-radius of 9999px and more is a capsule, as CSS draws pills.
static void read_radius(const style_sheet_t *s, style_query_t q, int *out) {
  int v;
  if (style_length(s, q, "border-radius", &v)) *out = v >= 9999 ? CORNER_RADIUS_CIRCULAR : v;
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
  read_radius(s, q_elem("Window", NULL), &t->window.corner_radius);
  read_radius(s, q_elem("Button", NULL), &t->button.corner_radius);
  read_radius(s, q_elem("TextEdit", NULL), &t->field.corner_radius);
  read_radius(s, q_elem("MultiEdit", NULL), &t->field.multiline_corner_radius);
  read_radius(s, q_elem("TabView", "tab"), &t->tab.corner_radius);

  read_radius(s, q_elem("Card", NULL), &t->card.corner_radius);
  read_role(s, q_elem("Card", NULL), "background-color", &t->card.face);
  read_role(s, q_elem("Card", NULL), "border-color", &t->card.outline);

  read_radius(s, q_elem("TableView", NULL), &t->table.corner_radius);
  style_length(s, q_elem("TableView", "header"), "min-height", &t->report.header_height);
  style_length(s, q_elem("TableView", "row"), "min-height", &t->report.row_height);
  int divider = t->report.row_dividers ? 1 : 0;
  if (style_length(s, q_elem("TableView", "row"), "border-bottom-width", &divider)) t->report.row_dividers = divider > 0;

  read_radius(s, q_elem("Badge", NULL), &t->badge.corner_radius);
  style_length(s, q_elem("Badge", NULL), "padding-left", &t->badge.padding);
  read_fraction(s, q_elem("Badge", NULL), "--tint-alpha", &t->badge.tint_alpha);
  read_fraction(s, q_elem("Badge", NULL), "--ink-mix", &t->badge.ink_mix);
  style_length(s, q_elem("Badge", "icon"), "width", &t->badge.icon_size);
  style_length(s, q_elem("Badge", "icon"), "margin-right", &t->badge.icon_gap);
}
