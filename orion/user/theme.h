#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include <stdbool.h>
#include <stdint.h>
#include <orion/user/user.h>
#include <orion/user/ui_style.h>

// Public drawing colors use sRGB RGB and linear alpha. Packed values are
// 0xAABBGGRR (R in the low byte); WEB(0xaabbcc) accepts CSS #RRGGBB.
#define WEB(hex) \
  (0xff000000u | ((uint32_t)((hex) & 0x000000ffu) << 16) | \
   ((uint32_t)((hex) & 0x0000ff00u)) | ((uint32_t)((hex) >> 16) & 0xffu))

// Themes own visual policy; controls own layout, input, values and notifications.
// Implement every callback below in theme_<name>.c and register its singleton
// in set_theme(). Callbacks receive logical bounds and explicit state; they
// must not inspect windows, send messages, mutate state or perform hit-testing.
//
// apply_palette() writes g_sys_colors before evThemeChanged is queued. Runtime
// set_sys_colors() overrides last until the next theme switch. The setter
// validates callbacks/metrics, cancels capture, queues theme notifications and
// invalidates roots; changed geometry also queues evResize.
// Theme definitions live for the process lifetime.

// Named spacing and geometry constants for widget draw code.
// Default dimension constants live in messages.h; theme-specific chrome
// geometry is stored in theme_t. This file
// covers the per-widget magic numbers that would otherwise appear as
// bare integer literals inside paint handlers.

// ── Theme icons ───────────────────────────────────────────────────────────
//
// Theme glyphs resolve to shared Lucide SVG assets via draw_theme_icon().
typedef enum {
  THEME_ICON_CLOSE        = 0,   // window close (x)
  THEME_ICON_ARROW_UP     = 1,   // up arrow, no tail (e.g. scroll-up button)
  THEME_ICON_ARROW_DOWN   = 2,   // down arrow, no tail
  THEME_ICON_ARROW_UPDOWN = 3,   // up-and-down arrow (combobox)
  THEME_ICON_CHECKMARK    = 4,   // checkmark (checkbox)
  THEME_ICON_SCROLL_UP    = 5,   // scroll-bar up
  THEME_ICON_SCROLL_RIGHT = 6,   // scroll-bar right
  THEME_ICON_SCROLL_DOWN  = 7,   // scroll-bar down
  THEME_ICON_SCROLL_LEFT  = 8,   // scroll-bar left
  THEME_ICON_RESIZE       = 9,   // resize grip (bottom-right window corner)
  THEME_ICON_MAXIMIZE     = 10,
  THEME_ICON_RESTORE      = 11,
  THEME_ICON_COUNT        = 12,
} theme_icon_t;

// Resolve a corner-radius metric for bounds r.
#define THEME_CORNER_RADIUS(metric, r) \
  ((metric) == CORNER_RADIUS_CIRCULAR ? MIN((r).w, (r).h) / 2 : MIN((metric), MIN((r).w, (r).h) / 2))

// Default logical size for compact control glyphs.
#define THEME_ICON_SIZE  9

// Corner radius metric value: round the short side fully (AppKit's circular bezel).
#define CORNER_RADIUS_CIRCULAR (-1)

// ── Text rendering ────────────────────────────────────────────────────────

// ── Buttons ───────────────────────────────────────────────────────────────

// Pixel inset from the button frame to the text/icon content area.
// Small enough to keep the label legible in 19px-tall buttons, but large
// enough to give text some breathing room inside the bevel.
#define BUTTON_PADDING    8

#define TEXTEDIT_PADDING_VERT 1   // vertical padding inside textedit controls (between frame and text)

// ── Checkboxes ────────────────────────────────────────────────────────────

// Pixels by which the focus-ring background extends beyond the box on
// each side (i.e. focus rect = box expanded by CHECKBOX_FOCUS_PAD).
#define CHECKBOX_BOX_SIZE    13
#define CHECKBOX_FOCUS_PAD   2

// Horizontal gap between the right edge of the box and the left edge of
// the label text.
#define CHECKBOX_GAP         6


// ── Theme drawing infrastructure ─────────────────────────────────────────────
//
// A theme_t is an immutable, process-lifetime drawing vtable.  Controls pass
// their computed bounds and semantic state; the active theme draws them without
// reading control internals, dispatching messages, or modifying state.
//
// Use get_theme() to read the current theme and set_theme() to switch at
// runtime.  All existing windows are notified via evThemeChanged.

// Independent interaction/render state flags.  Represent orthogonal boolean
// dimensions; do NOT collapse CTRL_SELECTED into CTRL_PRESSED.
typedef enum {
  CTRL_NORMAL   = 0,
  CTRL_NO_CLOSE = 1 << 6,   // window chrome omits the close button
  CTRL_HOVER    = 1 << 0,   // pointer is inside the hit target
  CTRL_PRESSED  = 1 << 1,   // mouse button is held down
  CTRL_SELECTED = 1 << 2,   // persistently active/checked/toggled
  CTRL_DISABLED = 1 << 3,   // input disabled; takes priority over transient states
  CTRL_FOCUSED  = 1 << 4,   // keyboard focus ring required
  CTRL_DEFAULT  = 1 << 5,   // primary action / default button
  CTRL_PLASTIC  = 1 << 6,   // paint a card as Card::plastic, the sheet's tinted material
  CTRL_MULTILINE = 1 << 7,  // field contains multiple lines
  CTRL_INACTIVE = 1 << 8,   // selected, but keyboard focus is elsewhere: a quieter selection
} ctrl_state_t;

typedef enum {
  THEME_CLASSIC = 0,  // bevels + dark palette
  THEME_MODERN  = 1,  // flat + dark palette (process default)
  THEME_LIGHT   = 2,  // flat + light WinUI palette
  THEME_NAVY    = 3,  // flat + blue palette
  THEME_AMBER   = 4,  // flat + light business palette: white wells, amber selection
  THEME_PORCELAIN = 5,  // flat + cool white business palette: white wells, indigo selection
  THEME_PORCELAIN_DARK = 6, // Porcelain's dark counterpart: graphite wells, indigo selection
  THEME_METRO = 7,  // square, flat dark creative workspace
  THEME_LAST      = THEME_METRO,
} theme_style_t;

// Makefile THEME_<app>=classic|modern|light|navy|amber|porcelain|porcelain_dark|metro → -DORION_THEME=<name>
#define THEME_classic  THEME_CLASSIC
#define THEME_modern   THEME_MODERN
#define THEME_light    THEME_LIGHT
#define THEME_navy     THEME_NAVY
#define THEME_amber    THEME_AMBER
#define THEME_porcelain THEME_PORCELAIN
#define THEME_porcelain_dark THEME_PORCELAIN_DARK
#define THEME_metro    THEME_METRO

// Semantic class/part identifiers, flattened into one enum to prevent invalid
// class/part pairs. Bounds and interaction state come from the caller.
typedef enum {
  THEME_PART_BUTTON,
  THEME_PART_CARD,            // a Card's box: CTRL_HOVER and CTRL_SELECTED are :hover and :checked; --tone is its accent edge
  THEME_PART_CHECKBOX,
  THEME_PART_COMBOBOX,
  THEME_PART_COMBOBOX_ARROW,
  THEME_PART_FIELD,
  THEME_PART_TAB,
  THEME_PART_TAB_PANE,
  THEME_PART_TAB_BAR,
  THEME_PART_SURFACE,
  THEME_PART_HEADER,
  THEME_PART_TOOLBAR,
  THEME_PART_TOOLBAR_BUTTON,
  THEME_PART_TOOLBAR_LABELED_BUTTON,
  THEME_PART_TOOLBAR_SPLIT_BUTTON,
  THEME_PART_TOOLBAR_SPLIT_ARROW,
  THEME_PART_TOOLBAR_SEPARATOR,
  THEME_PART_TOOLBAR_GRIP,
  THEME_PART_PANEL,
  THEME_PART_SIDEBAR,         // source-list column beside content (TabView sidebar style)
  THEME_PART_PANEL_BORDER,
  THEME_PART_WINDOW_BORDER,
  THEME_PART_RESIZE_GRIP,
  THEME_PART_TITLEBAR,
  THEME_PART_WINDOW_CLOSE,
  THEME_PART_STATUSBAR,
  THEME_PART_MENU_BAR,
  THEME_PART_MENU_POPUP,
  THEME_PART_MENU_ITEM,
  THEME_PART_SEPARATOR,
  THEME_PART_LIST_ITEM,
  THEME_PART_SLIDER_TRACK,
  THEME_PART_SLIDER_THUMB,
  THEME_PART_SLIDER_TROUGH,   // SLIDER_TROUGH: rounded well filling the control across its axis
  THEME_PART_SLIDER_KNOB,     // SLIDER_TROUGH: thumb inset in the trough; CTRL_PRESSED while dragged
  THEME_PART_SEGMENTED_TRACK, // trough behind a SegmentedControl's segments
  THEME_PART_SEGMENT,         // one segment; CTRL_SELECTED marks the chosen one
  THEME_PART_SCROLLBAR_TRACK,
  THEME_PART_SCROLLBAR_THUMB,
  THEME_PART_SCROLLBAR_ARROW_UP,
  THEME_PART_SCROLLBAR_ARROW_DOWN,
  THEME_PART_SCROLLBAR_ARROW_LEFT,
  THEME_PART_SCROLLBAR_ARROW_RIGHT,
  THEME_PART_SCROLLBAR_CORNER,
  THEME_PART_PAGE,           // workspace behind a Page's pane tree
  THEME_PART_TITLE_TOOLBAR,  // merged caption and toolbar row (WINDOW_TITLETOOLBAR), past any sidebar
  THEME_PART_COLUMN_DIVIDER, // report/table column boundary; CTRL_HOVER marks the resize handle
  THEME_PART_LIST_GROUP,     // a grouped list's caption row: its face, and its text colour as the foreground
  THEME_PART_LIST_BODY,      // the well behind a list's rows (report, icon and grid views)
  THEME_PART_ROW_RULE,       // the hairline under a row, where the theme rules rows
  THEME_PART_LIST_FRAME,     // the outline of a framed list (RVS_EX_FRAME)
  THEME_PART_ICON_ITEM,      // a row of an icon list; CTRL_SELECTED is the chosen one, its foreground the label
  THEME_PART_ICON_CELL,      // the face behind a chosen icon and label in an icon grid
  THEME_PART_THUMBNAIL,      // a photo-grid tile; CTRL_SELECTED draws its ring
  THEME_PART_DESKTOP_ICON,   // a desktop icon; CTRL_SELECTED frames it, its foreground is the caption
  THEME_PART_WORKSPACE,      // behind images and desktop icons: the document mat
  THEME_PART_DESKTOP,        // the desktop behind root windows
  THEME_PART_TILE_GRID,      // the well behind a TileGrid's tiles
  THEME_PART_CARET,          // a text field's insertion point
  THEME_PART_SPLITTER,       // a split view's handle; CTRL_HOVER highlights the strip the caller passes
  THEME_PART_DROP_TARGET,    // where a dragged pane will dock in a Page
  THEME_PART_DOCK_PREVIEW,   // where a dragged window will dock
  THEME_PART_DISABLED_VEIL,  // laid over a disabled window that does not paint its own disabled state
  THEME_PART_STACK_VIEW,     // a layout container's own box, like a div: nothing unless a sheet styles it
  THEME_PART_GRID_VIEW,
  THEME_PART_FLOW_VIEW,
  THEME_PART_BADGE,          // a status pill, toned by --tone
  THEME_PART_BADGE_ICON,     // the glyph before its text (fill is the icon's colour)
  THEME_PART_CARD_PLASTIC,   // Card::plastic, what THEME_PART_CARD paints with CTRL_PLASTIC; its tint is the instance variable --surface
  THEME_PART_COUNT,
} theme_part_t;

typedef struct style_sheet_s style_sheet_t;

// Drawing vtable.  All callbacks receive logical coordinates and explicit state.
// They must not dispatch messages, change control state, or perform hit-testing.
// Implementations may call fill_rect() and other low-level draw primitives.
typedef struct {
  theme_style_t  style;
  const char    *name;
  struct {
    bool dark;                  // palette is dark (≈ NSAppearanceNameDarkAqua)
    theme_style_t counterpart;  // same theme in the other appearance; its own style when there is none
  } appearance;
  // Every metric is a style sheet property, read with ui_px() and its kin (ui_style.h).

  // CSS themes: the theme's sheet (embedded text), read after
  // orion/themes/base.css. theme_stylesheet() parses them on first use.
  const char    *css;
  const char    *css_origin;
  style_sheet_t *sheet;
} theme_t;


static inline void theme_copy_palette(const uint32_t *src) {
  for (int i = 0; i < brCount; i++) g_sys_colors[i] = src[i];
}

// Active-theme accessor — never returns NULL. First use applies the
// process default (Modern, or the standalone -DORION_THEME). Gems do not
// compile a default; get_theme() is already the shell's theme.
theme_t *get_theme(void);

// Switch the active theme.  Validates the candidate; if valid, applies the
// theme palette via apply_palette() (which writes g_sys_colors), broadcasts
// evThemeChanged to all windows, and invalidates all roots.  Returns false
// and leaves the current theme unchanged on validation failure.
bool set_theme(theme_style_t style);

static inline int theme_toolbar_band_height(void) {
  return ui_px("Toolbar::button", "height") + 2 * ui_px("Toolbar", "padding");
}

// Paint semantic parts only inside the established paint path. Disabled state
// suppresses hover/pressed feedback while preserving selection and focus.
void theme_draw(theme_part_t part, irect16_t r, ctrl_state_t state);
// The part as it appears in win (≈ DrawThemeBackground with OpenThemeData(hwnd)):
// the window's classes and #id join the part's selector and its app sheet
// applies, so `TableView#checkouts::header` styles one table.
void theme_draw_in(const window_t *win, theme_part_t part, irect16_t r, ctrl_state_t state);
// How far win's part paints beyond its rect (outer shadows, outline): left, top, right, bottom.
irect16_t theme_ink_overflow_in(const window_t *win, theme_part_t part, ctrl_state_t state);
// How far win's part reserves inside its rect for margin and border: left, top, right, bottom.
irect16_t theme_content_insets_in(const window_t *win, theme_part_t part, ctrl_state_t state);
// theme_draw_in() and theme_foreground_in() with explicit instance variables (ui_style.h) in
// place of the window's own: a part toned for a row or cell that is not a window. NULL = none.
void theme_draw_with(const window_t *win, const ui_style_vars_t *vars, theme_part_t part, irect16_t r, ctrl_state_t state);
uint32_t theme_foreground_with(const window_t *win, const ui_style_vars_t *vars, theme_part_t part, ctrl_state_t state);
// The part's `fill`: the colour of the glyph it carries (its foreground when the sheet sets none).
uint32_t theme_fill_with(const window_t *win, const ui_style_vars_t *vars, theme_part_t part, ctrl_state_t state);

// Extra inputs for parts whose look depends on more than bounds and state.
typedef struct {
  uint32_t color;       // the plastic card's tint, handed to its sheet as --surface
} theme_draw_opts_t;
// theme_draw() with opts; CTRL_PLASTIC in state paints the card as Card::plastic.
void theme_draw_ex(theme_part_t part, irect16_t r, ctrl_state_t state, const theme_draw_opts_t *opts);
// The area a part leaves for content (≈ GetThemeBackgroundContentRect).
irect16_t theme_content_rect(theme_part_t part, irect16_t r, ctrl_state_t state);
uint32_t theme_foreground(theme_part_t part, ctrl_state_t state);
uint32_t theme_foreground_in(const window_t *win, theme_part_t part, ctrl_state_t state);

// Built-in theme singletons. Every theme but Classic shares Modern drawing;
// Amber and Porcelain share its business metrics.
theme_t *theme_classic_instance(void);
theme_t *theme_modern_instance(void);
theme_t *theme_light_instance(void);
theme_t *theme_navy_instance(void);
theme_t *theme_amber_instance(void);
theme_t *theme_porcelain_instance(void);
theme_t *theme_porcelain_dark_instance(void);
theme_t *theme_metro_instance(void);

// ── Style sheets ──────────────────────────────────────────────────────────────
//
// CSS-driven themes (orion/themes/*.css, embedded at build time): :root custom
// properties are the system colour roles, named in kebab case (brControlBg is
// --control-bg), control properties set theme_t metrics, and every theme part
// is painted from its box properties. See docs/styling.md.
const char *theme_sys_color_name(int role);      // "control-bg"; NULL for an unused slot
int  theme_sys_color_role(const char *name);     // -1 when no role has that name
// Every role from the sheet's :root; false (missing roles logged) when any is undefined.
bool theme_palette_from_stylesheet(style_sheet_t *sheet, uint32_t palette[brCount]);

// base.css + theme->css (or the file ORION_THEME_CSS names), in the theme's
// appearance; colour roles resolve against the live palette.
style_sheet_t *theme_stylesheet(theme_t *theme);
// An application's own sheet, read after the theme's as a page's sheet follows
// the user agent's; NULL removes it. Applies to every theme.
void ui_set_app_stylesheet(const char *css, const char *origin);
// An application instance's sheet (a gem's), read by the windows it owns
// through ui_selector(); instance 0 is the process and sets the app sheet.
// NULL removes it. A form with a stylesheet sets it when it is created.
void ui_set_instance_stylesheet(hinstance_t hinstance, const char *css, const char *origin);
style_sheet_t *ui_instance_stylesheet(hinstance_t hinstance);   // NULL: the instance reads the process sheet
bool ui_has_instance_stylesheet(hinstance_t hinstance);
// Painting from the active sheet (theme_box.c, theme_css.c, theme_builtin.c).
void theme_css_draw_part(theme_part_t part, irect16_t r, ctrl_state_t state, const ui_scope_t *scope);
uint32_t theme_css_foreground(theme_part_t part, ctrl_state_t state, const ui_scope_t *scope);
// The same for an instance with its own custom properties (vars may be NULL).
void theme_css_draw_part_vars(theme_part_t part, irect16_t r, ctrl_state_t state, const ui_scope_t *scope, const ui_style_vars_t *vars);
uint32_t theme_css_foreground_vars(theme_part_t part, ctrl_state_t state, const ui_scope_t *scope, const ui_style_vars_t *vars);
uint32_t theme_css_fill_vars(theme_part_t part, ctrl_state_t state, const ui_scope_t *scope, const ui_style_vars_t *vars);
irect16_t theme_css_insets_vars(theme_part_t part, ctrl_state_t state, const ui_scope_t *scope, const ui_style_vars_t *vars);
void theme_css_apply_palette(void);   // writes the sheet's colour roles into g_sys_colors
void theme_draw_window_chrome(irect16_t titlebar, irect16_t caption, const char *title, ctrl_state_t state, bool maximizable);
void theme_draw_statusbar_text(irect16_t r, const char *text);
// Engraved text: the control's own `text-shadow` in the sheet; disabled text has none.
int  theme_text_inset(ui_font_t font);
void theme_draw_text_clipped(ui_font_t font, const char *text, irect16_t r, uint32_t color, ctrl_state_t state);
void theme_draw_text_wrapped(ui_font_t font, const char *text, irect16_t r, uint32_t color, ctrl_state_t state);
void theme_draw_button_label(const window_t *win, irect16_t r, const char *text, ctrl_state_t state);
// Text of a control, styled by its selector in its state: the sheet's text-shadow
// (`CheckBox:disabled { text-shadow: none }`) decides whether and where it is engraved.
void theme_draw_text_clipped(const char *selector, const char *text, irect16_t r, uint32_t color, ctrl_state_t state);
void theme_draw_text_wrapped(const char *selector, const char *text, irect16_t r, uint32_t color, ctrl_state_t state);
int  theme_text_inset(const char *selector);   // room a box leaves for the text-shadow of selector
// The pseudo-classes (STYLE_*) a control state matches, and a selector carrying them (ui_style.c).
uint32_t theme_style_state(ctrl_state_t state);
const char *ui_state_selector(const char *selector, ctrl_state_t state);
void theme_draw_combobox(const window_t *win, irect16_t r, const char *text, ctrl_state_t state);

// Tests observe what would be painted: while a recorder is set, parts and the
// window chrome are reported to it instead of drawn. NULL paints again.
typedef struct {
  void (*part)(theme_part_t part, irect16_t r, ctrl_state_t state);
  void (*chrome)(irect16_t titlebar, irect16_t caption, const char *title, ctrl_state_t state, bool maximizable);
} theme_paint_recorder_t;
void theme_set_paint_recorder(const theme_paint_recorder_t *recorder);
void theme_paint_window_chrome(irect16_t titlebar, irect16_t caption, const char *title, ctrl_state_t state, bool maximizable);
// Bumped whenever cached styles go stale: a theme switch or a palette override.
extern unsigned g_theme_style_generation;

// ── Appearance (light / dark) ────────────────────────────────────────────────
//
// Each theme names its counterpart in the other appearance (Light ↔ Modern,
// Porcelain ↔ Porcelain Dark). With APPEARANCE_SYSTEM, the default, a visible
// UI switches to the variant matching the OS at start-up and whenever the OS
// appearance changes (kEventAppearanceChanged). LIGHT and DARK pin a variant.
// Themes without a counterpart (Classic, Navy, Amber) keep their palette.
// set_theme() chooses the theme; the next OS change or set_appearance() call
// may switch it to its counterpart. Hidden (headless) contexts never follow the OS.
typedef enum {
  APPEARANCE_SYSTEM = 0,
  APPEARANCE_LIGHT  = 1,
  APPEARANCE_DARK   = 2,
} appearance_t;

void set_appearance(appearance_t appearance);
appearance_t get_appearance(void);
// Window system: the OS appearance is now dark (true) or light (false).
void theme_system_appearance_changed(bool dark);

// Standalone binaries compiled with -DORION_THEME=<name> select that theme
// before main(). Gems omit the define, so get_theme() is the shell's theme.
#ifdef ORION_THEME
#define ORION_THEME_PASTE(name) THEME_##name
#define ORION_THEME_VALUE(name) ORION_THEME_PASTE(name)
static void orion_theme_ctor(void) __attribute__((constructor, used));
static void orion_theme_ctor(void) {
  set_theme(ORION_THEME_VALUE(ORION_THEME));
}
#endif

#endif /* __UI_THEME_H__ */
