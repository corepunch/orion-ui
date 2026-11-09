#include "test_framework.h"
#include <orion/user/style.h>
#include <orion/user/theme.h>

static style_query_t q(const char *element, const char *part, const char *classes, uint32_t state) {
  return (style_query_t){ .element = element, .part = part, .classes = classes, .state = state };
}

static const char *value(style_sheet_t *s, style_query_t query, const char *prop) {
  static char buf[256];
  return style_value(s, query, prop, buf, sizeof(buf)) ? buf : "(unset)";
}

void test_selectors_and_cascade(void) {
  TEST("style: elements, parts, classes, ids and states cascade by specificity, then order");
  style_sheet_t *s = style_sheet_parse(
    "Button { color: a; padding: 1px }\n"
    "Button { color: b }\n"
    "Button.primary { color: c }\n"
    "Button:hover { color: d }\n"
    "#save { color: e }\n"
    "* { margin: 3px }\n"
    "TableView::row, TableView::header { min-height: 21px }\n"
    "TableView::row:checked:window-inactive { background: f }\n"
    "Button { padding: 9px !important }\n"
    "Button.primary { padding: 2px }\n", "cascade.css");
  ASSERT_NOT_NULL(s);
  ASSERT_EQUAL(style_sheet_error_count(s), 0);
  ASSERT_STR_EQUAL(value(s, q("Button", NULL, NULL, 0), "color"), "b");
  ASSERT_STR_EQUAL(value(s, q("Button", NULL, "big primary", 0), "color"), "c");
  ASSERT_STR_EQUAL(value(s, q("Button", NULL, "primary", STYLE_HOVER), "color"), "d");   // later, same specificity
  style_query_t save = q("Button", NULL, "primary", STYLE_HOVER); save.id = "save";
  ASSERT_STR_EQUAL(value(s, save, "color"), "e");
  ASSERT_STR_EQUAL(value(s, q("Label", NULL, NULL, 0), "margin"), "3px");
  ASSERT_STR_EQUAL(value(s, q("Button", NULL, "primary", 0), "padding"), "9px");          // !important wins
  ASSERT_STR_EQUAL(value(s, q("TableView", "header", NULL, 0), "min-height"), "21px");
  ASSERT_STR_EQUAL(value(s, q("TableView", NULL, NULL, 0), "min-height"), "(unset)");    // the part is not the element
  ASSERT_STR_EQUAL(value(s, q("TableView", "row", NULL, STYLE_CHECKED), "background"), "(unset)");
  ASSERT_STR_EQUAL(value(s, q("TableView", "row", NULL, STYLE_CHECKED | STYLE_WINDOW_INACTIVE | STYLE_FOCUS), "background"), "f");
  ASSERT_STR_EQUAL(value(s, STYLE_ROOT, "color"), "(unset)");
  style_sheet_free(s);
  PASS();
}

void test_custom_properties_and_media(void) {
  TEST("style: var() resolves on the element, then :root, then the fallback; media follows the appearance");
  style_sheet_t *s = style_sheet_parse(
    ":root { --accent: #4C5BD4; --edge: var(--accent); --a: var(--b); --b: var(--a) }\n"
    "Card { --accent: #FF0000; border-color: var(--edge); color: var(--missing, var(--accent)) }\n"
    "Label { border: 1px solid var(--edge) }\n"
    "Badge { color: var(--a) } Chip { color: var(--nope) }\n"
    "@media (prefers-color-scheme: dark) { :root { --accent: #568AF2 } Label { color: white } }\n"
    "@media (prefers-color-scheme: light) { Label { color: black } }\n", "vars.css");
  ASSERT_EQUAL(style_sheet_error_count(s), 0);
  uint32_t c = 0;
  ASSERT_TRUE(style_color(s, q("Card", NULL, NULL, 0), "border-color", &c));
  ASSERT_EQUAL(c, WEB(0xFF0000));   // --edge's var(--accent) resolves on the queried element
  ASSERT_TRUE(style_color(s, q("Card", NULL, NULL, 0), "color", &c));
  ASSERT_EQUAL(c, WEB(0xFF0000));
  ASSERT_STR_EQUAL(value(s, q("Label", NULL, NULL, 0), "border"), "1px solid #4C5BD4");
  ASSERT_STR_EQUAL(value(s, q("Label", NULL, NULL, 0), "color"), "black");
  ASSERT_STR_EQUAL(value(s, q("Badge", NULL, NULL, 0), "color"), "(unset)");    // a var() cycle
  ASSERT_STR_EQUAL(value(s, q("Chip", NULL, NULL, 0), "color"), "(unset)");     // undefined, no fallback
  char name[32];
  ASSERT_TRUE(style_var_name(s, q("Card", NULL, NULL, 0), "border-color", name, sizeof(name)));
  ASSERT_STR_EQUAL(name, "edge");
  ASSERT_FALSE(style_var_name(s, q("Card", NULL, NULL, 0), "color", name, sizeof(name)));
  style_sheet_set_dark(s, true);
  ASSERT_STR_EQUAL(value(s, q("Label", NULL, NULL, 0), "color"), "white");
  ASSERT_TRUE(style_color(s, STYLE_ROOT, "--edge", &c));
  ASSERT_EQUAL(c, WEB(0x568AF2));
  style_sheet_free(s);
  PASS();
}

void test_error_recovery(void) {
  TEST("style: bad rules and declarations are skipped with the rest of the sheet intact");
  style_sheet_t *s = style_sheet_parse(
    "/* comment { not a rule } */\n"
    "Button Label { color: red }\n"          // combinator: rule dropped
    "Button:visited, Label { color: red }\n" // one bad selector drops the rule
    "Label { color; margin: 2px; : x; padding: }\n"
    "@import url(x.css);\n"
    "@font-face { font-family: X; src: url(x) }\n"
    "@media print { Label { margin: 9px } }\n"
    "Edit { color: blue }\n"
    "Edit { padding: 4px\n", "broken.css");
  ASSERT_NOT_NULL(s);
  ASSERT_EQUAL(style_sheet_error_count(s), 9);
  ASSERT_STR_EQUAL(value(s, q("Label", NULL, NULL, 0), "margin"), "2px");
  ASSERT_STR_EQUAL(value(s, q("Label", NULL, NULL, 0), "color"), "(unset)");
  ASSERT_STR_EQUAL(value(s, q("Edit", NULL, NULL, 0), "color"), "blue");
  ASSERT_STR_EQUAL(value(s, q("Edit", NULL, NULL, 0), "padding"), "(unset)");
  ASSERT_EQUAL(style_sheet_rule_count(s), 3);
  style_sheet_free(s);
  ASSERT_NULL(style_sheet_load("/nonexistent/theme.css"));
  PASS();
}

void test_value_parsers(void) {
  TEST("style: colours and px lengths parse the CSS notations");
  uint32_t c = 0;
  ASSERT_TRUE(style_parse_color("#4C5BD4", &c));            ASSERT_EQUAL(c, WEB(0x4C5BD4));
  ASSERT_TRUE(style_parse_color("#fff", &c));               ASSERT_EQUAL(c, 0xFFFFFFFFu);
  ASSERT_TRUE(style_parse_color("#00000040", &c));          ASSERT_EQUAL(c, 0x40000000u);
  ASSERT_TRUE(style_parse_color("#f008", &c));              ASSERT_EQUAL(c, 0x880000FFu);
  ASSERT_TRUE(style_parse_color("rgb(255, 0, 0)", &c));     ASSERT_EQUAL(c, 0xFF0000FFu);
  ASSERT_TRUE(style_parse_color("rgb(0 128 255 / 50%)", &c)); ASSERT_EQUAL(c, 0x80FF8000u);
  ASSERT_TRUE(style_parse_color("rgba(0,0,0,0.25)", &c));   ASSERT_EQUAL(c, 0x40000000u);
  ASSERT_TRUE(style_parse_color(" transparent ", &c));      ASSERT_EQUAL(c, 0u);
  ASSERT_FALSE(style_parse_color("#12345", &c));
  ASSERT_FALSE(style_parse_color("rgb(1 2)", &c));
  ASSERT_FALSE(style_parse_color("indigo", &c));
  int n = 0;
  ASSERT_TRUE(style_parse_length("21px", &n));   ASSERT_EQUAL(n, 21);
  ASSERT_TRUE(style_parse_length("0", &n));      ASSERT_EQUAL(n, 0);
  ASSERT_TRUE(style_parse_length("4.6px", &n));  ASSERT_EQUAL(n, 5);
  ASSERT_TRUE(style_parse_length("-2px", &n));   ASSERT_EQUAL(n, -2);
  ASSERT_FALSE(style_parse_length("2em", &n));
  ASSERT_TRUE(style_parse_length("12", &n));     ASSERT_EQUAL(n, 12);
  ASSERT_FALSE(style_parse_length("px", &n));
  PASS();
}

void test_porcelain_sheet(void) {
  TEST("theme sheets: no errors, every colour role in both appearances, and Porcelain reads them");
  const char *sheets[] = {"base", "modern", "navy", "amber", "metro", "porcelain"};
  for (int i = 0; i < 6; i++) {
    char path[64]; snprintf(path, sizeof(path), "orion/themes/%s.css", sheets[i]);
    style_sheet_t *sheet = style_sheet_load(path);
    ASSERT_NOT_NULL(sheet);
    ASSERT_EQUAL(style_sheet_error_count(sheet), 0);
    if (i > 0) {
      ASSERT_TRUE(style_sheet_append(sheet, "", NULL));
      uint32_t colors[brCount];
      ASSERT_TRUE(theme_palette_from_stylesheet(sheet, colors));
      style_sheet_set_dark(sheet, true);
      ASSERT_TRUE(theme_palette_from_stylesheet(sheet, colors));
    }
    style_sheet_free(sheet);
  }
  style_sheet_t *s = style_sheet_load("orion/themes/porcelain.css");
  ASSERT_NOT_NULL(s);
  uint32_t palette[brCount];
  ASSERT_TRUE(theme_palette_from_stylesheet(s, palette));
  ASSERT_EQUAL(palette[brAccent], WEB(0x4C5BD4));
  ASSERT_EQUAL(palette[brBorderFocus], palette[brAccent]);
  style_sheet_set_dark(s, true);
  ASSERT_TRUE(theme_palette_from_stylesheet(s, palette));
  ASSERT_EQUAL(palette[brAccent], WEB(0x568AF2));
  ASSERT_EQUAL(palette[brBorderFocus], WEB(0x568AF2));
  ASSERT_EQUAL(palette[brModalOverlay], 0x60000000u);
  style_sheet_free(s);

  ASSERT_TRUE(set_theme(THEME_PORCELAIN) || get_theme()->style == THEME_PORCELAIN);
  ASSERT_EQUAL(get_sys_color(brColumnViewBg), WEB(0xFFFFFF));
  ASSERT_EQUAL(ui_radius("Button"), 6);
  ASSERT_EQUAL(ui_role("Card", "background-color"), brColumnViewBg);
  ASSERT_EQUAL(ui_role("Card", "border-color"), brLightEdge);
  ASSERT_EQUAL(ui_px("TableView::row", "min-height"), 21);
  ASSERT_TRUE(ui_flag("TableView::row", "--divider-width"));
  ASSERT_EQUAL(((int)(ui_number("Badge", "--tint-alpha") * 255.0f + 0.5f)), 0x26);
  ASSERT_TRUE(set_theme(THEME_PORCELAIN_DARK));
  ASSERT_EQUAL(get_sys_color(brTextNormal), WEB(0xABB2BF));
  ASSERT_EQUAL(ui_radius("TextEdit"), 5);
  ASSERT_EQUAL(theme_sys_color_role("control-bg"), brControlBg);
  ASSERT_EQUAL(theme_sys_color_role("nope"), -1);
  ASSERT_STR_EQUAL(theme_sys_color_name(brTooltipText), "tooltip-text");
  PASS();
}

static bool live_var(const char *name, char *out, size_t n, void *ctx) {
  (void)ctx;
  if (strcmp(name, "accent")) return false;
  snprintf(out, n, "#00FF00");
  return true;
}
static bool platform_var(const char *name, char *out, size_t n, void *ctx) {
  (void)ctx;
  if (strcmp(name, "control-radius")) return false;
  snprintf(out, n, "13px");
  return true;
}

void test_mixing_hooks_and_append(void) {
  TEST("style: color-mix, live and fallback variables, appended sheets and ranked lookups");
  uint32_t c = 0;
  ASSERT_TRUE(style_parse_color("color-mix(in srgb, #000000, #FFFFFF)", &c));          ASSERT_EQUAL(c, WEB(0x808080));
  ASSERT_TRUE(style_parse_color("color-mix(in srgb, #FF0000 25%, #0000FF)", &c));      ASSERT_EQUAL(c, WEB(0x4000BF));
  ASSERT_TRUE(style_parse_color("color-mix(in srgb, #4C5BD4 14.9%, transparent)", &c)); ASSERT_EQUAL(c, 0x26D45B4Cu);
  ASSERT_TRUE(style_parse_color("color-mix(in srgb, rgb(0 0 0), white 20%)", &c));     ASSERT_EQUAL(c, WEB(0x333333));
  ASSERT_FALSE(style_parse_color("color-mix(in oklch, red, blue)", &c));
  ASSERT_FALSE(style_parse_color("color-mix(in srgb, #000)", &c));

  style_sheet_t *s = style_sheet_parse(":root { --accent: #FF0000; --radius: var(--control-radius) }\n"
                                       "Button { color: var(--accent); border-radius: var(--radius) }\n", "base.css");
  ASSERT_TRUE(style_sheet_append(s, "Button { border: 1px solid red }\nButton { border-color: blue }\n", "theme.css"));
  ASSERT_EQUAL(style_sheet_rule_count(s), 4);
  style_sheet_set_var_hook(s, live_var, platform_var, NULL);
  ASSERT_TRUE(style_color(s, q("Button", NULL, NULL, 0), "color", &c));
  ASSERT_EQUAL(c, WEB(0x00FF00));                                     // the live value wins over :root
  ASSERT_STR_EQUAL(value(s, q("Button", NULL, NULL, 0), "border-radius"), "13px");   // the fallback fills a gap
  char v[64]; long shorthand, longhand;
  ASSERT_TRUE(style_value_ranked(s, q("Button", NULL, NULL, 0), "border", v, sizeof(v), &shorthand));
  ASSERT_TRUE(style_value_ranked(s, q("Button", NULL, NULL, 0), "border-color", v, sizeof(v), &longhand));
  ASSERT_TRUE(longhand > shorthand);
  ASSERT_FALSE(style_value_ranked(s, q("Button", NULL, NULL, 0), "outline", v, sizeof(v), &longhand));
  ASSERT_EQUAL(longhand, -1);
  style_sheet_free(s);
  PASS();
}

int main(void) {
  TEST_START("Style sheets");
  test_selectors_and_cascade();
  test_custom_properties_and_media();
  test_error_recovery();
  test_value_parsers();
  test_mixing_hooks_and_append();
  test_porcelain_sheet();
  TEST_END();
}
