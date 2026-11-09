// The CSS box painter: draws a theme part from the cascaded box properties of
// its element and part (orion/themes/base.css lists them).
//
//   background        a colour, none, or linear-gradient([to bottom,] top, bottom)
//   border, border-<side>, border-width/-style/-color, border-<side>-<property>
//                     solid sides; a uniform border follows the corner radius
//   border-radius     1..4 lengths (top-left, top-right, bottom-right, bottom-left);
//                     9999px is a capsule; rounded top and square bottom is a tab
//   margin            1..4 lengths: the painted box inside the part's bounds
//   outline           width style colour, filled behind the box (a focus halo)
//   box-shadow        a list of [inset] dx dy [blur] colour: outer shadows paint
//                     behind the box (blurred like CSS); inset ones with no blur
//                     paint the edges they offset into, inside the border (bevels)
//   color             the part's text colour (theme_foreground)
//   content           the glyph drawn over the box: arrow-updown, close,
//                     maximize, checkmark, resize, scroll-up/-down/-left/-right,
//                     triangle-down, grip, dots, groove, or none
//   fill              the glyph's colour; defaults to color
//   stroke            the second line of a groove
//
// Boxes are cached per part and state until the theme or palette changes.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "theme.h"
#include "style.h"

typedef struct {
  unsigned generation;
  bool background, gradient;
  uint32_t top, bottom;
  int border_width[4];        // top, right, bottom, left
  uint32_t border_color[4];
  int radius[4];              // top-left, top-right, bottom-right, bottom-left; -1 capsule
  int margin[4];
  int outline_width;
  uint32_t outline_color;
  uint32_t color, fill, stroke;
  int glyph;
  int shadow_count;
  struct { bool inset; int dx, dy, blur; uint32_t color; } shadows[4];
} theme_box_t;

enum { GLYPH_NONE, GLYPH_ARROW_UPDOWN, GLYPH_CLOSE, GLYPH_MAXIMIZE, GLYPH_CHECKMARK, GLYPH_RESIZE,
       GLYPH_SCROLL_UP, GLYPH_SCROLL_DOWN, GLYPH_SCROLL_LEFT, GLYPH_SCROLL_RIGHT,
       GLYPH_TRIANGLE_DOWN, GLYPH_GRIP, GLYPH_DOTS, GLYPH_GROOVE };
static const char *const k_glyph_names[] = {
  "none", "arrow-updown", "close", "maximize", "checkmark", "resize", "scroll-up", "scroll-down",
  "scroll-left", "scroll-right", "triangle-down", "grip", "dots", "groove",
};

static const struct { const char *element, *part, *classes; } k_part_selectors[THEME_PART_COUNT] = {
  [THEME_PART_BUTTON]                 = {"Button"},
  [THEME_PART_CARD]                   = {"Card"},
  [THEME_PART_CHECKBOX]               = {"CheckBox", "box"},
  [THEME_PART_COMBOBOX]               = {"ComboBox"},
  [THEME_PART_COMBOBOX_ARROW]         = {"ComboBox", "arrow"},
  [THEME_PART_FIELD]                  = {"TextEdit"},
  [THEME_PART_TAB]                    = {"TabView", "tab"},
  [THEME_PART_TAB_PANE]               = {"TabView", "pane"},
  [THEME_PART_TAB_BAR]                = {"TabView", "bar"},
  [THEME_PART_SURFACE]                = {"Surface"},
  [THEME_PART_HEADER]                 = {"TableView", "header"},
  [THEME_PART_TOOLBAR]                = {"Toolbar"},
  [THEME_PART_TOOLBAR_BUTTON]         = {"Toolbar", "button"},
  [THEME_PART_TOOLBAR_LABELED_BUTTON] = {"Toolbar", "button", "labeled"},
  [THEME_PART_TOOLBAR_SPLIT_BUTTON]   = {"Toolbar", "button", "split"},
  [THEME_PART_TOOLBAR_SPLIT_ARROW]    = {"Toolbar", "split-arrow"},
  [THEME_PART_TOOLBAR_SEPARATOR]      = {"Toolbar", "separator"},
  [THEME_PART_TOOLBAR_GRIP]           = {"Toolbar", "grip"},
  [THEME_PART_PANEL]                  = {"Panel"},
  [THEME_PART_SIDEBAR]                = {"Sidebar"},
  [THEME_PART_PANEL_BORDER]           = {"Panel", "border"},
  [THEME_PART_WINDOW_BORDER]          = {"Window", "border"},
  [THEME_PART_RESIZE_GRIP]            = {"Window", "resize-grip"},
  [THEME_PART_TITLEBAR]               = {"Window", "titlebar"},
  [THEME_PART_WINDOW_CLOSE]           = {"Window", "close"},
  [THEME_PART_STATUSBAR]              = {"StatusBar"},
  [THEME_PART_MENU_BAR]               = {"MenuBar"},
  [THEME_PART_MENU_POPUP]             = {"Menu"},
  [THEME_PART_MENU_ITEM]              = {"Menu", "item"},
  [THEME_PART_SEPARATOR]              = {"Separator"},
  [THEME_PART_LIST_ITEM]              = {"TableView", "row"},
  [THEME_PART_SLIDER_TRACK]           = {"Slider", "track"},
  [THEME_PART_SLIDER_THUMB]           = {"Slider", "thumb"},
  [THEME_PART_SLIDER_TROUGH]          = {"Slider", "trough"},
  [THEME_PART_SLIDER_KNOB]            = {"Slider", "knob"},
  [THEME_PART_SEGMENTED_TRACK]        = {"SegmentedControl"},
  [THEME_PART_SEGMENT]                = {"SegmentedControl", "segment"},
  [THEME_PART_SCROLLBAR_TRACK]        = {"Scrollbar", "track"},
  [THEME_PART_SCROLLBAR_THUMB]        = {"Scrollbar", "thumb"},
  [THEME_PART_SCROLLBAR_ARROW_UP]     = {"Scrollbar", "arrow-up"},
  [THEME_PART_SCROLLBAR_ARROW_DOWN]   = {"Scrollbar", "arrow-down"},
  [THEME_PART_SCROLLBAR_ARROW_LEFT]   = {"Scrollbar", "arrow-left"},
  [THEME_PART_SCROLLBAR_ARROW_RIGHT]  = {"Scrollbar", "arrow-right"},
  [THEME_PART_SCROLLBAR_CORNER]       = {"Scrollbar", "corner"},
  [THEME_PART_PAGE]                   = {"Page"},
  [THEME_PART_TITLE_TOOLBAR]          = {"Window", "title-toolbar"},
  [THEME_PART_COLUMN_DIVIDER]         = {"TableView", "divider"},
  [THEME_PART_LIST_GROUP]             = {"TableView", "group"},
  [THEME_PART_LIST_BODY]              = {"TableView", "body"},
  [THEME_PART_ROW_RULE]               = {"TableView", "rule"},
  [THEME_PART_LIST_FRAME]             = {"TableView", "frame"},
  [THEME_PART_ICON_ITEM]              = {"IconView", "item"},
  [THEME_PART_ICON_CELL]              = {"IconView", "cell"},
  [THEME_PART_THUMBNAIL]              = {"IconView", "thumbnail"},
  [THEME_PART_DESKTOP_ICON]           = {"Icon"},
  [THEME_PART_WORKSPACE]              = {"Workspace"},
  [THEME_PART_DESKTOP]                = {"Desktop"},
  [THEME_PART_TILE_GRID]              = {"TileGrid"},
  [THEME_PART_CARET]                  = {"TextEdit", "caret"},
  [THEME_PART_SPLITTER]               = {"SplitView", "handle"},
  [THEME_PART_DROP_TARGET]            = {"Page", "drop-target"},
  [THEME_PART_DOCK_PREVIEW]           = {"Dock", "preview"},
  [THEME_PART_DISABLED_VEIL]          = {"Window", "disabled-veil"},
  [THEME_PART_STACK_VIEW]             = {"StackView"},
  [THEME_PART_GRID_VIEW]              = {"GridView"},
  [THEME_PART_FLOW_VIEW]              = {"FlowView"},
};

// ── value parsing ─────────────────────────────────────────────────────────────

// Splits value into whitespace-separated tokens, keeping parentheses together.
static int box_tokens(char *value, char *tok[], int max) {
  int n = 0, depth = 0;
  char *c = value;
  while (*c && n < max) {
    while (isspace((unsigned char)*c)) c++;
    if (!*c) break;
    tok[n++] = c;
    for (; *c && (depth || !isspace((unsigned char)*c)); c++) {
      if (*c == '(') depth++;
      else if (*c == ')' && depth) depth--;
    }
    if (*c) *c++ = 0;
  }
  return n;
}

// 1..4 values in CSS order expanded to four sides.
static bool box_four(const char *value, int out[4], bool radius) {
  char buf[128]; snprintf(buf, sizeof(buf), "%s", value);
  char *tok[4]; int n = box_tokens(buf, tok, 4), v[4];
  if (n < 1) return false;
  for (int i = 0; i < n; i++) {
    if (!style_parse_length(tok[i], &v[i])) return false;
    if (radius && v[i] >= 9999) v[i] = -1;
  }
  int map[5][4] = {{0}, {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
  for (int i = 0; i < 4; i++) out[i] = v[map[n][i]];
  return true;
}

static bool box_style_keyword(const char *t) {
  static const char *k[] = {"none", "hidden", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset"};
  for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (!strcmp(t, k[i])) return true;
  return false;
}

static bool box_width_token(const char *t, int *out) {
  if (!strcmp(t, "thin")) { *out = 1; return true; }
  if (!strcmp(t, "medium")) { *out = 3; return true; }
  if (!strcmp(t, "thick")) { *out = 5; return true; }
  return style_parse_length(t, out);
}

// A border or outline shorthand: width, style and colour in any order.
typedef struct { int width; char style[16]; uint32_t color; bool has_color; } box_line_t;

static bool box_line(const char *value, box_line_t *line) {
  char buf[256]; snprintf(buf, sizeof(buf), "%s", value);
  char *tok[4]; int n = box_tokens(buf, tok, 4);
  line->width = 3; snprintf(line->style, sizeof(line->style), "none"); line->has_color = false;
  for (int i = 0; i < n; i++) {
    if (box_style_keyword(tok[i])) snprintf(line->style, sizeof(line->style), "%s", tok[i]);
    else if (box_width_token(tok[i], &line->width)) {}
    else if (style_parse_color(tok[i], &line->color)) line->has_color = true;
    else return false;
  }
  return true;
}

static const char *const k_sides[4] = {"top", "right", "bottom", "left"};

// One component (0 width, 1 style, 2 colour) of one side: the highest-ranked of
// border-<side>-<component>, border-<side>, border-<component> and border.
static bool box_border_component(const style_sheet_t *s, style_query_t q, int side, int comp, char *out, size_t n) {
  static const char *const comps[3] = {"width", "style", "color"};
  char props[4][40], values[4][256];
  long ranks[4]; int best = -1;
  snprintf(props[0], sizeof(props[0]), "border-%s-%s", k_sides[side], comps[comp]);
  snprintf(props[1], sizeof(props[1]), "border-%s", k_sides[side]);
  snprintf(props[2], sizeof(props[2]), "border-%s", comps[comp]);
  snprintf(props[3], sizeof(props[3]), "border");
  for (int i = 0; i < 4; i++) {
    if (!style_value_ranked(s, q, props[i], values[i], sizeof(values[i]), &ranks[i])) continue;
    if (best < 0 || ranks[i] > ranks[best]) best = i;
  }
  if (best < 0) return false;
  if (best == 0) { snprintf(out, n, "%s", values[0]); return true; }
  if (best == 2) {   // a longhand of 1..4 values
    char buf[256]; snprintf(buf, sizeof(buf), "%s", values[2]);
    char *tok[4]; int count = box_tokens(buf, tok, 4);
    if (count < 1) return false;
    int map[5][4] = {{0}, {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
    snprintf(out, n, "%s", tok[map[count][side]]);
    return true;
  }
  box_line_t line;
  if (!box_line(values[best], &line)) return false;
  if (comp == 0) snprintf(out, n, "%dpx", line.width);
  else if (comp == 1) snprintf(out, n, "%s", line.style);
  else if (line.has_color) snprintf(out, n, "#%02X%02X%02X%02X", line.color & 0xFF, (line.color >> 8) & 0xFF, (line.color >> 16) & 0xFF, line.color >> 24);
  else return false;   // currentColor
  return true;
}

// What the painter cannot draw yet is said, never drawn differently in silence.
static void box_unsupported(const char *what, theme_part_t part, const char *id) {
  fprintf(stderr, "[theme] part %s%s%s%s%s: %s is not supported yet\n", k_part_selectors[part].element,
          id && *id ? "#" : "", id && *id ? id : "",
          k_part_selectors[part].part ? "::" : "", k_part_selectors[part].part ? k_part_selectors[part].part : "", what);
  fflush(stderr);
}

static void box_report(const char *what, theme_part_t part, const char *value) {
  fprintf(stderr, "[theme] part %s%s%s: bad %s \"%s\"\n", k_part_selectors[part].element,
          k_part_selectors[part].part ? "::" : "", k_part_selectors[part].part ? k_part_selectors[part].part : "", what, value);
  fflush(stderr);
}

// scope: the window's classes, #id and instance sheet (ui_window_scope), or NULL.
static void box_compute(theme_box_t *b, theme_part_t part, ctrl_state_t state, const ui_scope_t *scope) {
  const style_sheet_t *s = scope ? ui_scope_stylesheet(scope) : theme_stylesheet(get_theme());
  memset(b, 0, sizeof(*b));
  char classes[160];
  const char *own = k_part_selectors[part].classes ? k_part_selectors[part].classes : "", *win_classes = ui_scope_classes(scope);
  snprintf(classes, sizeof(classes), "%s%s%s%s", own, *own && *win_classes ? " " : "", win_classes,
           (state & CTRL_MULTILINE) ? " multiline" : "");
  const char *id = ui_scope_id(scope);
  style_query_t q = {
    .element = k_part_selectors[part].element, .part = k_part_selectors[part].part, .classes = classes, .id = *id ? id : NULL,
    .state = ((state & CTRL_HOVER) ? STYLE_HOVER : 0) | ((state & CTRL_PRESSED) ? STYLE_ACTIVE : 0) |
             ((state & CTRL_SELECTED) ? STYLE_CHECKED : 0) | ((state & CTRL_DISABLED) ? STYLE_DISABLED : 0) |
             ((state & CTRL_FOCUSED) ? STYLE_FOCUS : 0) | ((state & CTRL_DEFAULT) ? STYLE_DEFAULT : 0) |
             ((state & CTRL_INACTIVE) ? STYLE_WINDOW_INACTIVE : 0),
  };
  b->color = get_sys_color(brTextNormal);
  style_color(s, q, "color", &b->color);
  b->fill = b->color;
  style_color(s, q, "fill", &b->fill);

  char v[256];
  if (style_value(s, q, "background", v, sizeof(v)) || style_value(s, q, "background-color", v, sizeof(v))) {
    if (!strcmp(v, "none") || !strcmp(v, "transparent")) {}
    else if (!strncmp(v, "linear-gradient(", 16)) {
      char *args = v + 16, *end = strrchr(args, ')');
      if (end) *end = 0;
      char *first = args, *comma = strchr(args, ',');
      if (comma && !strncmp(first + strspn(first, " "), "to bottom", 9)) { first = comma + 1; comma = strchr(first, ','); }
      // split at the comma between the two colours, outside parentheses
      int depth = 0; comma = NULL;
      for (char *c = first; *c; c++) { if (*c == '(') depth++; else if (*c == ')') depth--; else if (*c == ',' && !depth) { comma = c; break; } }
      if (comma) *comma = 0;
      if (comma && style_parse_color(first, &b->top) && style_parse_color(comma + 1, &b->bottom)) b->background = b->gradient = true;
      else box_report("background", part, v);
    } else if (style_parse_color(v, &b->top)) { b->bottom = b->top; b->background = (b->top >> 24) != 0; }
    else box_report("background", part, v);
  }

  for (int side = 0; side < 4; side++) {
    char width[64], style[64], color[64];
    int w = 3;
    if (!box_border_component(s, q, side, 1, style, sizeof(style))) snprintf(style, sizeof(style), "none");
    if (!strcmp(style, "none") || !strcmp(style, "hidden")) continue;
    if (box_border_component(s, q, side, 0, width, sizeof(width)) && !box_width_token(width, &w)) { box_report("border width", part, width); w = 0; }
    b->border_color[side] = b->color;
    if (box_border_component(s, q, side, 2, color, sizeof(color)) && !style_parse_color(color, &b->border_color[side]))
      box_report("border colour", part, color);
    b->border_width[side] = w;
  }

  if (style_value(s, q, "content", v, sizeof(v))) {
    size_t g = 0;
    while (g < sizeof(k_glyph_names) / sizeof(k_glyph_names[0]) && strcmp(v, k_glyph_names[g])) g++;
    if (g < sizeof(k_glyph_names) / sizeof(k_glyph_names[0])) b->glyph = (int)g;
    else box_report("content", part, v);
  }
  b->stroke = b->fill;
  style_color(s, q, "stroke", &b->stroke);
  if (style_value(s, q, "box-shadow", v, sizeof(v)) && strcmp(v, "none")) {
    for (char *item = v, *next; item && b->shadow_count < 4; item = next) {
      int depth = 0; next = NULL;
      for (char *c = item; *c; c++) {
        if (*c == '(') depth++;
        else if (*c == ')') depth--;
        else if (*c == ',' && !depth) { *c = 0; next = c + 1; break; }
      }
      char *tok[6]; int n = box_tokens(item, tok, 6), lengths[4], nl = 0;
      bool inset = false, ok = true; uint32_t color = 0xFF000000u;
      for (int i = 0; i < n && ok; i++) {
        if (!strcmp(tok[i], "inset")) inset = true;
        else if (nl < 4 && style_parse_length(tok[i], &lengths[nl])) nl++;
        else ok = style_parse_color(tok[i], &color);
      }
      if (!ok || nl < 2) { box_report("box-shadow", part, item); break; }
      if (nl > 3 && lengths[3] != 0) box_unsupported("a box-shadow spread", part, id);
      if (inset && nl > 2 && lengths[2] != 0) box_unsupported("a blurred inset box-shadow (drawn unblurred)", part, id);
      b->shadows[b->shadow_count].inset = inset;
      b->shadows[b->shadow_count].dx = lengths[0];
      b->shadows[b->shadow_count].dy = lengths[1];
      b->shadows[b->shadow_count].blur = nl > 2 ? lengths[2] : 0;
      b->shadows[b->shadow_count].color = color;
      b->shadow_count++;
    }
  }

  if (style_value(s, q, "border-radius", v, sizeof(v)) && !box_four(v, b->radius, true)) box_report("border-radius", part, v);
  const int *rad = b->radius, *bw = b->border_width;
  bool one_radius = rad[0] == rad[1] && rad[1] == rad[2] && rad[2] == rad[3];
  bool top_only = rad[0] == rad[1] && rad[2] == 0 && rad[3] == 0;
  if (!one_radius && !top_only) box_unsupported("a border-radius other than one radius or the top two", part, id);
  bool uniform = bw[0] == bw[1] && bw[1] == bw[2] && bw[2] == bw[3] && b->border_color[0] == b->border_color[1] &&
                 b->border_color[1] == b->border_color[2] && b->border_color[2] == b->border_color[3];
  if (!uniform && (rad[0] || rad[1] || rad[2] || rad[3])) box_unsupported("a border with unequal sides on a rounded box", part, id);
  if (style_value(s, q, "margin", v, sizeof(v)) && !box_four(v, b->margin, false)) box_report("margin", part, v);
  if (style_value(s, q, "outline", v, sizeof(v))) {
    box_line_t line;
    if (!box_line(v, &line)) box_report("outline", part, v);
    else if (strcmp(line.style, "none")) { b->outline_width = line.width; b->outline_color = line.has_color ? line.color : b->color; }
  }
}

// Parts × the state bits that select styles (CTRL_PLASTIC/CTRL_NO_CLOSE excluded).
static theme_box_t *g_box_cache[THEME_PART_COUNT];

static unsigned box_state_key(ctrl_state_t state) {
  return (state & 0x3F) | ((state & CTRL_MULTILINE) ? 0x40 : 0) | ((state & CTRL_INACTIVE) ? 0x80 : 0);
}

// Windows with classes, an #id or an instance sheet: their own boxes.
#define BOX_SCOPES 128
static struct { const ui_scope_t *scope; theme_box_t *boxes[THEME_PART_COUNT]; } g_scoped_boxes[BOX_SCOPES];
static int g_scoped_box_count;

static theme_box_t **box_slots(const ui_scope_t *scope) {
  if (!scope) return g_box_cache;
  for (int i = 0; i < g_scoped_box_count; i++) if (g_scoped_boxes[i].scope == scope) return g_scoped_boxes[i].boxes;
  if (g_scoped_box_count == BOX_SCOPES) {
    fprintf(stderr, "[theme] more than %d styled windows; #%s paints as its element\n", BOX_SCOPES, ui_scope_id(scope));
    fflush(stderr);
    return NULL;
  }
  g_scoped_boxes[g_scoped_box_count].scope = scope;
  return g_scoped_boxes[g_scoped_box_count++].boxes;
}

static const theme_box_t *box_for(theme_part_t part, ctrl_state_t state, const ui_scope_t *scope) {
  static theme_box_t scratch;
  theme_box_t **slots = box_slots(scope);
  if (!slots) { slots = g_box_cache; scope = NULL; }
  if (!slots[part]) slots[part] = calloc(256, sizeof(theme_box_t));
  theme_box_t *b = slots[part] ? &slots[part][box_state_key(state)] : &scratch;
  if (b->generation != g_theme_style_generation || !b->generation) {
    box_compute(b, part, state, scope);
    b->generation = g_theme_style_generation;
  }
  return b;
}

// ── painting ──────────────────────────────────────────────────────────────────

static int box_radius(int v, irect16_t r) {
  int full = (MIN(r.w, r.h) + 1) / 2;
  return v < 0 ? full : MIN(v, full);
}

static void box_fill(uint32_t top, uint32_t bottom, bool gradient, irect16_t r, int radius, bool top_only) {
  if (r.w <= 0 || r.h <= 0) return;
  if (gradient || top_only) {
    if (top_only) fill_gradient_rounded_top_rect(top, bottom, r, radius);
    else fill_gradient_rounded_rect(top, bottom, r, radius);
  } else if (radius > 0) fill_rounded_rect(top, r, radius);
  else fill_rect(top, r);
}

static void box_paint(const theme_box_t *b, irect16_t r) {
  r = R(r.x + b->margin[3], r.y + b->margin[0], r.w - b->margin[1] - b->margin[3], r.h - b->margin[0] - b->margin[2]);
  if (r.w <= 0 || r.h <= 0) return;
  if (b->outline_width > 0) fill_rect(b->outline_color, rect_inset(r, -b->outline_width));
  const int *rad = b->radius;
  bool top_only = rad[0] == rad[1] && rad[0] != 0 && rad[2] == 0 && rad[3] == 0;
  int widest = MAX(MAX(rad[0] < 0 ? 9999 : rad[0], rad[1] < 0 ? 9999 : rad[1]), MAX(rad[2] < 0 ? 9999 : rad[2], rad[3] < 0 ? 9999 : rad[3]));
  int radius = box_radius(top_only ? rad[0] : (widest >= 9999 ? -1 : widest), r);
  const int *bw = b->border_width;
  bool uniform = bw[0] == bw[1] && bw[1] == bw[2] && bw[2] == bw[3] &&
                 b->border_color[0] == b->border_color[1] && b->border_color[1] == b->border_color[2] &&
                 b->border_color[2] == b->border_color[3];
  for (int i = b->shadow_count - 1; i >= 0; i--) {   // outer shadows, the first on top
    if (b->shadows[i].inset) continue;
    draw_rect_shadow(r, (float)radius, (float)b->shadows[i].blur, (ipoint16_t){b->shadows[i].dx, b->shadows[i].dy},
                     b->shadows[i].color);
  }
  bool opaque = b->background && (b->top >> 24) == 0xFF && (b->bottom >> 24) == 0xFF;
  if (uniform && bw[0] > 0 && (opaque || (radius > 0 && !top_only))) {
    uint32_t edge = b->border_color[0];
    if (opaque) {   // the face covers the middle of the edge-coloured shape
      box_fill(edge, edge, false, r, radius, top_only);
      box_fill(b->top, b->bottom, b->gradient, rect_inset(r, bw[0]), MAX(0, radius - bw[0]), top_only);
    } else {
      if (b->background) box_fill(b->top, b->bottom, b->gradient, rect_inset(r, bw[0]), MAX(0, radius - bw[0]), top_only);
      stroke_rounded_rect(edge, r, radius, bw[0]);
    }
  } else {
    if (b->background) box_fill(b->top, b->bottom, b->gradient, r, radius, top_only);
    if (bw[0] > 0 || bw[1] > 0 || bw[2] > 0 || bw[3] > 0) fill_border_sides(r, bw, b->border_color);
  }
  irect16_t inner = R(r.x + bw[3], r.y + bw[0], r.w - bw[1] - bw[3], r.h - bw[0] - bw[2]);
  for (int i = b->shadow_count - 1; i >= 0; i--) {   // inset edges, the first on top
    if (!b->shadows[i].inset || inner.w <= 0 || inner.h <= 0) continue;
    int dx = b->shadows[i].dx, dy = b->shadows[i].dy;
    uint32_t c = b->shadows[i].color;
    if (dy > 0) fill_rect(c, rect_split_top(inner, MIN(dy, inner.h)));
    if (dx > 0) fill_rect(c, rect_split_left(inner, MIN(dx, inner.w)));
    if (dy < 0) fill_rect(c, rect_split_bottom(inner, MIN(-dy, inner.h)));
    if (dx < 0) fill_rect(c, rect_split_right(inner, MIN(-dx, inner.w)));
  }
}

// The box's content glyph, in its fill colour.
static void box_glyph(const theme_box_t *b, irect16_t r) {
  uint32_t ink = b->fill;
  static const int icons[] = {
    [GLYPH_ARROW_UPDOWN] = THEME_ICON_ARROW_UPDOWN, [GLYPH_CLOSE] = THEME_ICON_CLOSE, [GLYPH_MAXIMIZE] = THEME_ICON_MAXIMIZE,
    [GLYPH_RESIZE] = THEME_ICON_RESIZE, [GLYPH_SCROLL_UP] = THEME_ICON_SCROLL_UP, [GLYPH_SCROLL_DOWN] = THEME_ICON_SCROLL_DOWN,
    [GLYPH_SCROLL_LEFT] = THEME_ICON_SCROLL_LEFT, [GLYPH_SCROLL_RIGHT] = THEME_ICON_SCROLL_RIGHT,
  };
  switch (b->glyph) {
    case GLYPH_NONE: break;
    case GLYPH_CHECKMARK: {
      irect16_t check = rect_center(r, MIN(r.w, r.h), MIN(r.w, r.h));
      draw_theme_icon(THEME_ICON_CHECKMARK, check.x, check.y, check.w, ink);
      break;
    }
    case GLYPH_TRIANGLE_DOWN: {
      int cx = r.x + r.w / 2, cy = r.y + r.h / 2 - 1;
      for (int i = 0; i < 4; i++) fill_rect(ink, R(cx - 3 + i, cy + i, 7 - 2 * i, 1));
      break;
    }
    case GLYPH_GRIP:
      if (r.w > r.h) fill_rect(ink, rect_center(r, MIN(16, r.w - 4), 2));
      else fill_rect(ink, rect_center(r, 2, MIN(16, r.h - 4)));
      break;
    case GLYPH_DOTS:
      for (int row = 0; row < 3; row++)
        for (int col = row; col < 3; col++) fill_rect(ink, R(r.x + r.w - 6 + col * 2, r.y + r.h - 6 + row * 2, 1, 1));
      break;
    case GLYPH_GROOVE:   // a fill line and a stroke line along the long axis, as an etched rule
      if (r.w > r.h) {
        int my = r.y + r.h / 2;
        fill_rect(ink, R(r.x + 2, my, r.w - 4, 1));
        fill_rect(b->stroke, R(r.x + 2, my + 1, r.w - 4, 1));
      } else {
        int mx = r.x + r.w / 2;
        fill_rect(ink, R(mx, r.y + 2, 1, r.h - 4));
        fill_rect(b->stroke, R(mx + 1, r.y + 2, 1, r.h - 4));
      }
      break;
    default: draw_theme_icon_in_rect(icons[b->glyph], r, ink); break;
  }
}

void theme_css_draw_part(theme_part_t part, irect16_t r, ctrl_state_t state, const ui_scope_t *scope) {
  if (part < 0 || part >= THEME_PART_COUNT || part == THEME_PART_CARD) return;   // cards paint through draw_card
  const theme_box_t *b = box_for(part, state, scope);
  box_paint(b, r);
  box_glyph(b, r);
}

// The extent draw_rect_shadow and the outline give the box beyond r.
irect16_t theme_ink_overflow_in(const window_t *win, theme_part_t part, ctrl_state_t state) {
  irect16_t ink = {0, 0, 0, 0};
  if (part < 0 || part >= THEME_PART_COUNT) return ink;
  const theme_box_t *b = box_for(part, state, ui_window_scope(win));
  int out[4] = {b->outline_width, b->outline_width, b->outline_width, b->outline_width};   // left, top, right, bottom
  for (int i = 0; i < b->shadow_count; i++) {
    if (b->shadows[i].inset) continue;
    int reach = (int)(MAX(b->shadows[i].blur * 0.5f, 0.25f) * 3 + 1);
    out[0] = MAX(out[0], reach - b->shadows[i].dx); out[2] = MAX(out[2], reach + b->shadows[i].dx);
    out[1] = MAX(out[1], reach - b->shadows[i].dy); out[3] = MAX(out[3], reach + b->shadows[i].dy);
  }
  int margin[4] = {b->margin[3], b->margin[0], b->margin[1], b->margin[2]};
  for (int i = 0; i < 4; i++) out[i] = MAX(0, out[i] - margin[i]);
  return (irect16_t){out[0], out[1], out[2], out[3]};
}

uint32_t theme_css_foreground(theme_part_t part, ctrl_state_t state, const ui_scope_t *scope) {
  if (part < 0 || part >= THEME_PART_COUNT) return get_sys_color(brTextNormal);
  return box_for(part, state, scope)->color;
}
