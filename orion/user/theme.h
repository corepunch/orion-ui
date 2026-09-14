#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include <stdbool.h>
#include <orion/user/user.h>

// Named spacing and geometry constants for widget draw code.
// Dimension constants that are owned by a specific subsystem (e.g.
// SCROLLBAR_WIDTH, TITLEBAR_HEIGHT) live in messages.h.  This file
// covers the per-widget magic numbers that would otherwise appear as
// bare integer literals inside paint handlers.

// ── Theme icons ───────────────────────────────────────────────────────────
//
// Indices into share/orion/theme.png (144x18 px grayscale, 9x9 tiles).
// The sheet contains 16 columns x 2 rows; only the first THEME_ICON_COUNT
// entries are defined.  Use draw_theme_icon() to render them.
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
  THEME_ICON_COUNT        = 10,
} theme_icon_t;

// Native tile size of theme icons in logical pixels.
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

#define TEXTEDIT_PADDING_HORZ 4  // horizontal padding inside textedit controls (between frame and text)
#define TEXTEDIT_PADDING_VERT 1   // vertical padding inside textedit controls (between frame and text)

// ── Checkboxes ────────────────────────────────────────────────────────────

// Pixels by which the focus-ring background extends beyond the box on
// each side (i.e. focus rect = box expanded by CHECKBOX_FOCUS_PAD).
#define CHECKBOX_BOX_SIZE    13
#define CHECKBOX_FOCUS_PAD   2

// Horizontal gap between the right edge of the box and the left edge of
// the label text.
#define CHECKBOX_GAP         6

// Window cornder radius for rounded-corner masking of the FBO texture during compositing.
#define WINDOW_CORNER_RADIUS 8

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
  CTRL_HOVER    = 1 << 0,   // pointer is inside the hit target
  CTRL_PRESSED  = 1 << 1,   // mouse button is held down
  CTRL_SELECTED = 1 << 2,   // persistently active/checked/toggled
  CTRL_DISABLED = 1 << 3,   // input disabled; takes priority over transient states
  CTRL_FOCUSED  = 1 << 4,   // keyboard focus ring required
  CTRL_DEFAULT  = 1 << 5,   // primary action / default button
} ctrl_state_t;

typedef enum {
  THEME_CLASSIC = 0,
  THEME_MODERN  = 1,
} theme_style_t;

// Toolbar item drawing variant.  Passed to draw_toolbar_item_bg so themes can
// distinguish cases where Classic's borderless exception applies.
typedef enum {
  TOOLBAR_VARIANT_BUTTON,          // TOOLBAR_ITEM_BUTTON, no labels
  TOOLBAR_VARIANT_BUTTON_LABELED,  // TOOLBAR_ITEM_BUTTON, SHOW_LABELS active
  TOOLBAR_VARIANT_DROPDOWN_BTN,    // TOOLBAR_ITEM_DROPDOWN, main button part
  TOOLBAR_VARIANT_DROPDOWN_ARROW,  // TOOLBAR_ITEM_DROPDOWN, arrow part
} toolbar_item_variant_t;

// Drawing vtable.  All callbacks receive logical coordinates and explicit state.
// They must not dispatch messages, change control state, or perform hit-testing.
// Implementations may call fill_rect() and other low-level draw primitives.
typedef struct {
  theme_style_t  style;
  const char    *name;

  void (*draw_bevel)(irect16_t r);
  void (*draw_button_bg)(irect16_t r, ctrl_state_t state);
  void (*draw_toolbar_item_bg)(irect16_t r, ctrl_state_t state,
                               toolbar_item_variant_t variant);
  void (*draw_toolbar_separator)(irect16_t r);
  void (*draw_panel_bg)(irect16_t r, bool resize_grip);
  void (*draw_titlebar_bg)(irect16_t r, bool focused);
  void (*draw_statusbar_bg)(irect16_t r);
  void (*draw_checkbox_box)(irect16_t r, bool checked, ctrl_state_t state);
  void (*draw_combobox_bg)(irect16_t r, ctrl_state_t state);
  void (*draw_list_item_bg)(irect16_t r, ctrl_state_t state);
  void (*draw_slider_thumb)(irect16_t r, bool active);
  void (*draw_menu_item_bg)(irect16_t r, ctrl_state_t state);

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
  int control_padding;       // standard inset from control frame to content area

  // Writes the theme's palette into g_sys_colors.  Called by set_theme()
  // before evThemeChanged is broadcast so controls see the new colors
  // immediately on first repaint.
  void (*apply_palette)(void);
} theme_t;

// Active-theme accessor — never returns NULL (defaults to Classic).
theme_t *get_theme(void);

// Switch the active theme.  Validates the candidate; if valid, updates
// g_sys_colors with the theme palette, broadcasts evThemeChanged to all
// windows, and invalidates all roots.  Returns false and leaves the current
// theme unchanged on validation failure.
bool set_theme(theme_style_t style);

// Built-in theme singletons.
theme_t *theme_classic_instance(void);
theme_t *theme_modern_instance(void);

#endif /* __UI_THEME_H__ */
