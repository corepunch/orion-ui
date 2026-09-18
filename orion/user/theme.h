#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include <stdbool.h>
#include <stdint.h>
#include <orion/user/user.h>

// WEB(0xaabbcc) is #aabbcc. Packs R in the low byte (fill_rect) with alpha 0xff.
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
// invalidates roots; a changed scrollbar gutter also queues evResize.
// Theme definitions live for the process lifetime.

// Named spacing and geometry constants for widget draw code.
// Dimension constants that are owned by a specific subsystem (e.g.
// SCROLLBAR_WIDTH, TITLEBAR_HEIGHT) live in messages.h.  This file
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

// Default logical size for compact control glyphs.
#define THEME_ICON_SIZE  9

// ── Text rendering ────────────────────────────────────────────────────────

// Standard 1-pixel drop-shadow offset used for all text labels.
// Shadow is drawn at (x + TEXT_SHADOW_OFFSET, y + TEXT_SHADOW_OFFSET)
// before the main text pass.
#define TEXT_SHADOW_OFFSET   1

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
} ctrl_state_t;

typedef enum {
  THEME_CLASSIC = 0,  // bevels + dark palette
  THEME_MODERN  = 1,  // flat + dark palette (process default)
  THEME_LIGHT   = 2,  // flat + light WinUI palette
  THEME_NAVY    = 3,  // flat + navy/purple palette
} theme_style_t;

// Makefile THEME_<app>=classic|modern|light|navy → -DORION_THEME=<name>
#define THEME_classic  THEME_CLASSIC
#define THEME_modern   THEME_MODERN
#define THEME_light    THEME_LIGHT
#define THEME_navy     THEME_NAVY

// Semantic class/part identifiers, flattened into one enum to prevent invalid
// class/part pairs. Bounds and interaction state come from the caller.
typedef enum {
  THEME_PART_BUTTON,
  THEME_PART_CHECKBOX,
  THEME_PART_COMBOBOX,
  THEME_PART_COMBOBOX_ARROW,
  THEME_PART_FIELD,
  THEME_PART_TAB,
  THEME_PART_TAB_PANE,
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
  THEME_PART_PANEL_BORDER,
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
  THEME_PART_SCROLLBAR_TRACK,
  THEME_PART_SCROLLBAR_THUMB,
  THEME_PART_SCROLLBAR_ARROW_UP,
  THEME_PART_SCROLLBAR_ARROW_DOWN,
  THEME_PART_SCROLLBAR_ARROW_LEFT,
  THEME_PART_SCROLLBAR_ARROW_RIGHT,
  THEME_PART_SCROLLBAR_CORNER,
  THEME_PART_COUNT,
} theme_part_t;

// Drawing vtable.  All callbacks receive logical coordinates and explicit state.
// They must not dispatch messages, change control state, or perform hit-testing.
// Implementations may call fill_rect() and other low-level draw primitives.
typedef struct {
  theme_style_t  style;
  const char    *name;

  void (*draw_part)(theme_part_t part, irect16_t r, ctrl_state_t state);
  void (*draw_window_chrome)(irect16_t titlebar, irect16_t caption,
                             const char *title, ctrl_state_t state, bool maximizable);
  void (*draw_statusbar_text)(irect16_t r, const char *text);
  void (*draw_button_label)(irect16_t r, const char *text, ctrl_state_t state);
  void (*draw_combobox)(irect16_t r, const char *text, ctrl_state_t state);
  uint32_t (*foreground)(theme_part_t part, ctrl_state_t state);

  // Scrollbar geometry policy.  scrollbar_overlay=true means Modern overlay
  // thumbs; no reserved gutter is allocated.
  int  scrollbar_width;    // reserved gutter width (pixels); 0 = overlay only
  bool scrollbar_overlay;  // true = overlay thumbs, false = reserved-space bars

  // Icon/label press offset in logical pixels.  Classic shifts content by 1
  // when a button is pressed to simulate physical depression; Modern keeps
  // content stationary and changes only the background.
  int press_icon_offset;

  // Per-theme geometry metrics used by draw code to avoid hardcoded literals.
  int button_corner_radius;  // rounded-corner radius for push buttons (0 = square)
  int window_corner_radius;  // logical pixels, applied by the SDF compositor
  int window_shadow_blur;    // logical pixels; zero disables shadows
  ipoint16_t window_shadow_offset;
  uint32_t window_shadow_color;
  struct {
    int hover, selected, selected_hover, pressed; // system color roles
  } item_background;
  int control_padding;       // standard inset from control frame to content area

  // Writes the theme's palette into g_sys_colors.  Called by set_theme()
  // before evThemeChanged is broadcast so controls see the new colors
  // immediately on first repaint.
  void (*apply_palette)(void);
} theme_t;

static inline bool theme_is_modern(const theme_t *t) {
  return t && t->style != THEME_CLASSIC;
}

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

// Paint semantic parts only inside the established paint path. Disabled state
// suppresses hover/pressed feedback while preserving selection and focus.
void theme_draw(theme_part_t part, irect16_t r, ctrl_state_t state);
uint32_t theme_foreground(theme_part_t part, ctrl_state_t state);

// Built-in theme singletons. Light and Navy share Modern drawing.
theme_t *theme_classic_instance(void);
theme_t *theme_modern_instance(void);
theme_t *theme_light_instance(void);
theme_t *theme_navy_instance(void);

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
