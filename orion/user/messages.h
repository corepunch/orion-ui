#ifndef __UI_MESSAGES_H__
#define __UI_MESSAGES_H__

#include "config.h"  // Tunable framework parameters

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// Window messages
enum {
  evCreate,
  evDestroy,
  evShowWindow,
  evNCPaint,
  evNCLeftButtonDown,
  evNCLeftButtonUp,
  evPaint,
  evRefreshStencil,
  evPaintStencil,
  evThemeChanged,   // wparam = theme_style_t; broadcast by set_theme() before invalidate
  evMouseActivate,
  evActivate,
  evDeactivate,
  evSetFocus,
  evKillFocus,
  evHitTest,
  // Sent to a parent before selected mouse/key events are delivered to a
  // child window, analogous to WinAPI WM_PARENTNOTIFY but consumable.
  // wparam = 0; lparam = parent_notify_t*. Return true to consume the event.
  evParentNotify,
  evCommand,
  evTextInput,
  evWheel,
  evMouseMove,
  evMouseLeave,
  evLeftButtonDown,
  evLeftButtonUp,
  evLeftButtonDoubleClick,
  evRightButtonDown,
  evRightButtonUp,
  evResize,
  evDisplayChange,
  evKeyDown,
  evKeyUp,
  evJoyButtonDown,
  evJoyButtonUp,
  evJoyAxisMotion,
  evStatusBar,
  // Sent to a window when its built-in horizontal/vertical scrollbar position
  // changes (analogous to WinAPI WM_HSCROLL / WM_VSCROLL).
  // wparam = new scroll position; lparam = NULL.
  evHScroll,
  evVScroll,
  // Sent when the user clicks the close (X) button on a non-dialog window.
  // Analogous to WM_CLOSE in WinAPI.
  // Return true  to cancel the close (e.g. show "unsaved changes?" dialog).
  // Return false to allow the default action (hide the window).
  evClose,
  // Delivered to a window when a timer registered with axSetTimer fires.
  // wparam = timer ID returned by axSetTimer.  lparam = userdata.
  // Analogous to WM_TIMER in WinAPI.
  evTimer,
  // Query a window for tooltip text at a given client position.
  // wparam = MAKEDWORD(client_x, client_y) — position inside the window's
  //          client area (same coordinate system as evMouseMove).
  // lparam = char[256] output buffer — write the NUL-terminated tooltip text
  //          here and return true; return false if no tooltip at that position.
  evGetTooltipText,
  // Query a window for the desired cursor shape at a given client position.
  // wparam = MAKEDWORD(client_x, client_y) — position inside the window's
  //          client area (same coordinate system as evMouseMove).
  // Return a curArrow .. curNotAllowed value; return curArrow for default.
  evGetCursor,
  // Measure / arrange messages for auto-layout containers.
  // evMeasure: lparam = layout_measure_t*; handler writes desired_w/desired_h
  //            into the struct and returns true. Return value is ignored.
  // evArrange: lparam = layout_arrange_t*; handler positions itself according
  //            to the provided rect.
  evMeasure,
  evArrange,
  // Query whether a component can be parented to a target window.
  // wparam = 0; lparam = window_t *target_parent.
  // Return true to reject the target parent; false to allow it.
  evCanParent,
  // Ask a container to create its default child structure.
  // wparam = 0; lparam = NULL.
  evInitChildren,
  // Set a database context on a window. Database-aware controls may consume
  // this to populate themselves; other recipients ignore it.
  // wparam = 0; lparam = database_t *.
  evSetDatabase,
  evUser = 1000
};

// Compatibility alias: callers that use evLayout map to evArrange.
#define evLayout evArrange

// Control messages
enum {
  btnSetCheck = evUser,
  btnGetCheck,
  btnSetImage,       // wparam = icon index (iBitmap); lparam = bitmap_strip_t*
  btnSetIconName,    // wparam = 0; lparam = const char* SVG base name (NULL to clear)
  cbAddString,
  cbGetCurrentSelection, // returns index; if lparam=int* also writes index (or kComboBoxError)
  cbGetCurrentValue,     // returns value_field data (e.g., ID) for foreign key binding
  cbSetCurrentSelection,
  cbGetListBoxText,
  cbClear,            // clear all items and reset title
  sbAddWindow,
  tbButtonClick,
  tbSetStrip,         // wparam=0, lparam=bitmap_strip_t* (or NULL to clear)
  tbSetActiveButton,  // wparam=ident of button to mark active
  sbSetInfo,        // lparam = scrollbar_info_t*
  sbGetPos,         // returns current scroll position
  slSetRange,       // lparam = slider_range_t* (min/max)
  slGetRange,       // lparam = slider_range_t* out (optional)
  slSetCount,       // wparam = handle count (1..4)
  slSetPos,         // wparam = handle index (0..3), lparam=(void*)(intptr_t)pos
  slGetPos,         // wparam = handle index (0..3), lparam=int* out(optional)
  tbSetButtonSize,    // wparam=square button size in pixels (0 resets to TB_SPACING)
  tbSetOrientation,   // wparam=toolbar_orientation_t
  tbDrawItem,         // paint-only callback: wparam=ident, lparam=toolbar_draw_item_t*
  tbSetStyle,         // wparam=TOOLBAR_STYLE_* flags
  tbLoadStrip,        // wparam=icon tile size in px (square); lparam=const char* path to PNG
  tbSetItems,         // wparam=count; lparam=toolbar_item_t* — set toolbar item list (owner-drawn)
  // Fired via evCommand when the user clicks the dropdown arrow of a TOOLBAR_ITEM_DROPDOWN button.
  // LOWORD(wparam) = button ident; HIWORD(wparam) = tbDropdown; lparam = toolbar window.
  tbDropdown,
  // Text edit getter/setter messages (single-line and multiline controls).
  // Getter pattern is WinAPI-like: return value in result_t and optionally
  // mirror it to lparam when non-NULL.
  edGetText,        // wparam=buf_size; lparam=char* dst → copies text, returns length
  edSetText,        // wparam=0; lparam=const char* src → replaces text
  // List (popup) messages
  lstSetItem,             // wparam=item index to pre-select in the dropdown list
  // Individual desktop-style icon control (commctl/icon.c).
  icSetImage,         // wparam=0; lparam=icon_image_t* (copied, texture not owned)
  icSetStatusImage,   // wparam=0; lparam=icon_image_t* (copied, NULL clears; drawn beside label)
  icSetBadge,         // wparam=slot; lparam=icon_badge_t* (copied, NULL clears)
  icClearBadges,      // clear every badge slot
  icSetSelected,      // wparam=0/1
  icGetSelected,      // returns 0/1
  icSetItemData,      // lparam=opaque application-owned pointer
  icGetItemData,      // returns opaque application-owned pointer
  // Gradient bar control (commctl/gradient.c)
  grSetColors,        // wparam=left_rgba; lparam=(void*)(uintptr_t)right_rgba
  // Async HTTP messages (analogous to WinInet/WinHTTP notifications).
  // Delivered to the window_t* registered with http_request_async() when the
  // request transitions through the following states:
  //
  //   evHttpDone     — request completed (success or failure).
  //     wparam = http_request_id_t (request handle).
  //     lparam = http_response_t*  (caller owns; free with http_response_free).
  //
  //   evHttpProgress — download progress update (optional, posted
  //     only when Content-Length is known).
  //     wparam = http_request_id_t.
  //     lparam = http_progress_t*  (framework-owned; valid only during
  //              message processing; do NOT retain or free).
  //
  // The request handle is returned by http_request_async().  A return value of
  // HTTP_INVALID_REQUEST indicates an immediate error (bad URL, OOM, etc.).
  evHttpDone,
  evHttpProgress,
};

// Control notification messages
enum {
  edUpdate = 100,
  btnClicked,
  cbSelectionChange,
  // Dialog Data Exchange normalized data-change notification.
  // Sent via evCommand when dialog state was updated regardless of the
  // originating control type. LOWORD(wparam)=source control id (if known),
  // HIWORD(wparam)=ddxDataChanged, lparam=pointer to dialog state/model.
  ddxDataChanged,
  sbChanged,  // wparam: MAKEDWORD(scrollbar_id, sbChanged); lparam: (void*)(intptr_t)new_pos
  // Icon notifications. lparam is the source icon window.
  icnClicked,
  icnSelectionChange,
  icnOpen,
  // Slider notifications sent via evCommand from win_slider.
  // LOWORD(wparam)=control id, HIWORD(wparam)=sliderValueChanged + handle_index.
  // handle 0 => sliderValueChanged, handle 1 => sliderValueChanged1, etc.
  // lparam = (void *)(intptr_t)new_value for the changed handle.
  sliderValueChanged,
  sliderValueChanged1,
  sliderValueChanged2,
  sliderValueChanged3,
  sliderValueChanged4,

  // Splitter notifications (win_splitter → parent via evCommand).
  //
  // spnDragStart — user pressed the mouse button on a splitter bar.
  //   wparam = MAKEDWORD(win->id, spnDragStart)
  //   lparam = MAKEDWORD(parent_local_x, parent_local_y)  (packed uint16_t coords)
  //
  // spnMoved — splitter position changed (sent on every mouse-move while dragging).
  //   Not sent by win_splitter itself; parent may use it to notify grandparents.
  spnDragStart,
  spnMoved,
};

// Button state
enum {
  btnStateUnchecked,
  btnStateChecked
};

// WM_ACTIVATE state codes (wparam for evActivate)
#define WA_INACTIVE    0
#define WA_ACTIVE      1
#define WA_CLICKACTIVE 2

// Error codes
#define kComboBoxError -1

// Window flags
#define WINDOW_NOTITLE      (1 << 0)
#define WINDOW_TRANSPARENT  (1 << 1)
#define WINDOW_VSCROLL      (1 << 2)
#define WINDOW_HSCROLL      (1 << 3)
#define WINDOW_NORESIZE     (1 << 4)
#define WINDOW_NOFILL       (1 << 5)
#define WINDOW_ALWAYSONTOP  (1 << 6)
#define WINDOW_ALWAYSINBACK (1 << 7)
#define WINDOW_HIDDEN       (1 << 8)
#define WINDOW_NOTRAYBUTTON (1 << 9)
#define WINDOW_DIALOG       (1 << 10)
#define WINDOW_TOOLBAR      (1 << 11)
#define WINDOW_STATUSBAR    (1 << 12)
// Button style flags (analogous to WinAPI BS_* styles)
// BUTTON_PUSHLIKE: button stays visually pressed while win->value == true (like a toggle/check button)
// BUTTON_AUTORADIO: clicking auto-clears all sibling AUTORADIO buttons and sets this one checked
// BUTTON_DEFAULT: analogous to BS_DEFPUSHBUTTON — drawn with a black outline; triggered by Enter
// Bitmap/image buttons are a separate window class (win_toolbar_button), not a flag on win_button.
// Use btnSetImage on a win_toolbar_button window to assign an icon from a bitmap_strip_t.
#define BUTTON_PUSHLIKE     (1 << 13)
#define BUTTON_AUTORADIO    (1 << 14)
#define BUTTON_DEFAULT      (1 << 15)
// Bit 16 is intentionally unused (the former WINDOW_SIDEBAR special case).
#define WINDOW_NOACTIVATE   (1 << 17)  // do not steal keyboard focus when shown
#define WINDOW_NOTABSTOP    (1 << 18)  // exclude from Tab-key focus cycle (WS_TABSTOP equivalent)
#define WINDOW_STACK_HORIZONTAL (1 << 19)  // auto-layout stack flows left-to-right
#define WINDOW_FLEXSPACE    (1 << 20)  // space/spring child that absorbs leftover horizontal room
#define WINDOW_AUTO_LAYOUT  (1 << 21)  // enable automatic measure/arrange for children
#define WINDOW_LAYOUT_CONTAINER (1 << 22)  // window arranges children (stack/grid/flow/column)
#define WINDOW_NODRAG       (1 << 23)  // fixed chrome/panel: never initiate a window drag
#define WINDOW_STACK_VERTICAL   0

// Runtime window state bits stored in window_t.flags. Bits 29-30 hold the
// CONTROL_SIZE_* style; the remaining upper bits are transient state.
#define WINDOW_STATE_HOVERED   (1u << 24)
#define WINDOW_STATE_EDITING   (1u << 25)
#define WINDOW_STATE_PRESSED   (1u << 26)
#define WINDOW_STATE_VISIBLE   (1u << 27)
#define WINDOW_STATE_DISABLED  (1u << 28)

// Auto-layout alignment values used by layout_measure_t / layout_arrange_t.
// 0 = stretch (default), matching WPF/SwiftUI "fill available space".
enum {
  LAYOUT_ALIGN_STRETCH = 0,
  LAYOUT_ALIGN_START   = 1,
  LAYOUT_ALIGN_CENTER  = 2,
  LAYOUT_ALIGN_END     = 3,
};

// Scroll bar constants (WinAPI-style, used with set_scroll_info / get_scroll_info)
#define SB_HORZ  0   // horizontal scroll bar
#define SB_VERT  1   // vertical scroll bar
#define SB_BOTH  3   // both scroll bars

#define SIF_RANGE  0x0001   // nMin and nMax are valid
#define SIF_PAGE   0x0002   // nPage is valid
#define SIF_POS    0x0004   // nPos is valid
#define SIF_ALL    (SIF_RANGE | SIF_PAGE | SIF_POS)

// Visibility mode constants for win_sb_t::visible_mode
// SB_VIS_AUTO: visibility is managed by set_scroll_info() auto show/hide heuristic
// SB_VIS_HIDE: bar is explicitly hidden (show_scroll_bar(false))
// SB_VIS_SHOW: bar is explicitly shown  (show_scroll_bar(true))
#define SB_VIS_AUTO  ((int8_t)-1)
#define SB_VIS_HIDE  ((int8_t) 0)
#define SB_VIS_SHOW  ((int8_t) 1)

// Width of a built-in scrollbar strip in logical pixels (also height of arrow buttons)
#define SCROLLBAR_WIDTH  13
// Scroll distance applied per arrow-button click (one logical unit).
#define SB_ARROW_STEP    1
// Pixel size (width and height) of an icon8 glyph
#define ICON8_SIZE       8

// When a window has both WINDOW_HSCROLL and WINDOW_STATUSBAR, the horizontal
// scrollbar is merged into the status-bar row.  The left fraction (%) is
// reserved for status text; the right fraction hosts the scrollbar thumb.
// This macro must be used by both the drawing code (draw_impl.c) and the
// hit-testing code (message.c) to guarantee they agree on the split point.
#define SB_STATUS_SPLIT_X(win_w)  ((win_w) * 20 / 100)

// Scroll info struct (analogous to WinAPI SCROLLINFO).
// Passed to set_scroll_info() / get_scroll_info().
typedef struct {
  uint32_t fMask;  // SIF_* flags indicating which fields are valid
  int      nMin;   // minimum scroll position
  int      nMax;   // maximum scroll position
  int      nPage;  // page size (viewport dimension along the scroll axis)
  int      nPos;   // current scroll position
} scroll_info_t;

// Chrome height constants — derived from FONT_SIZE (defined in kernel/kernel.h).
// FONT_SIZE (FONT_SYSTEM): 12 at UI_WINDOW_SCALE==1, 8 at scale>=2.
// FONT_SIZE_SMALL (FONT_SMALL): 12 at UI_WINDOW_SCALE==1, 8 at scale>=2.
// Content heights (list rows, column-view entries) use FONT_SIZE_SMALL;
// see commctl/columnview.h.  All values are usable in static initializers.
// +5 instead of +4 keeps the height odd at both scales so that 9x9 theme
// icons centre with equal integer padding on every side.
#define SYSICON_SIZE      24              // canonical size of sysicon/toolbar SVG tiles
#define TITLEBAR_HEIGHT   (FONT_SIZE + 5)
#if defined(__APPLE__) && TARGET_OS_IOS
#define TOOLBAR_HEIGHT    42
#define BUTTON_HEIGHT     40
#else
#define TOOLBAR_HEIGHT    38
#define BUTTON_HEIGHT     25
#endif
#define STATUSBAR_HEIGHT  (FONT_SIZE + 5)
#define CONTROL_HEIGHT_MINI     13
#define CONTROL_HEIGHT_SMALL    16
#define CONTROL_HEIGHT_REGULAR  BUTTON_HEIGHT
#define CONTROL_HEIGHT_LARGE    24
#define WINDOW_PADDING 4
#define LINE_PADDING 5
#define CONTROL_HEIGHT 14

// macOS-style intrinsic sizes for button-like controls.  These are styles,
// not dimensions: Button, TextEdit, and ComboBox own their height.
// Regular is zero so existing callers receive the platform default.
#define CONTROL_SIZE_REGULAR  (0u << 29)
#define CONTROL_SIZE_SMALL    (1u << 29)
#define CONTROL_SIZE_MINI     (2u << 29)
#define CONTROL_SIZE_LARGE    (3u << 29)
#define CONTROL_SIZE_MASK     (3u << 29)

#define TB_SPACING              TOOLBAR_HEIGHT  // equals TOOLBAR_HEIGHT so toolbar buttons are square
#define TOOLBAR_PADDING         2               // pixels of margin between toolbar border and button area (all sides)
#define TOOLBAR_SPACING         1               // minimal pixel gap between consecutive toolbar elements
#define TOOLBAR_SPACING_GAP_WIDTH  4            // pixels of gap inserted by a TOOLBAR_ITEM_SPACER entry
#define TOOLBAR_BEVEL_WIDTH     1               // width of the bevel border drawn around the toolbar button area (each side)
#define TOOLBAR_BAND_HEIGHT     (TB_SPACING + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH))
#define TOOLBAR_LABEL_PADDING           8       // horizontal padding added to auto-computed label width (left+right)
#define TOOLBAR_COMBOBOX_DEFAULT_WIDTH_MULT  3  // default combobox width = button_size * this multiplier
#define TOOLBAR_BUTTON_FLAG_ACTIVE   (1u << 0)
#define TOOLBAR_BUTTON_FLAG_PRESSED  (1u << 1)
#define TOOLBAR_STYLE_SHOW_LABELS    (1u << 0) // WinAPI-style text below button icons
#define DROPDOWN_ARROW_W             12          // pixel width of the dropdown arrow zone in TOOLBAR_ITEM_DROPDOWN

typedef enum {
  TOOLBAR_HORIZONTAL = 0,
  TOOLBAR_VERTICAL,
} toolbar_orientation_t;

// Toolbar item types used with tbSetItems.
typedef enum {
  TOOLBAR_ITEM_BUTTON    = 0,  // icon-only button (owner-drawn)
  TOOLBAR_ITEM_LABEL     = 1,  // static text label (owner-drawn)
  TOOLBAR_ITEM_COMBOBOX  = 2,  // drop-down combobox (embedded child window)
  TOOLBAR_ITEM_TEXTEDIT  = 3,  // single-line text input (embedded child window)
  TOOLBAR_ITEM_SEPARATOR = 4,  // narrow visual separator (owner-drawn)
  TOOLBAR_ITEM_SPACER    = 5,  // invisible gap (owner-drawn, no interaction)
  TOOLBAR_ITEM_DROPDOWN  = 6,  // split button: left half fires tbButtonClick, right arrow fires tbDropdown
  TOOLBAR_ITEM_CUSTOM,        // drawn by the owner during tbDrawItem
} toolbar_item_type_t;

// Descriptor for a single toolbar item (used with tbSetItems).
typedef struct {
  toolbar_item_type_t type;   // item type
  int                 ident;  // command ID / button identifier
  const char         *icon;   // BUTTON: SVG base name, e.g. "git-fork", "undo"; NULL = no icon
  int                 w;      // explicit width in pixels (0 = automatic)
  uint32_t            flags;  // extra style flags (BUTTON_PUSHLIKE, BUTTON_AUTORADIO, …)
  const char         *text;   // label text, or combobox/textedit initial text
  const char         *tooltip; // tooltip text shown on hover; NULL = none
} toolbar_item_t;

// Tab control messages and notifications (WinAPI TCM_*/TCN_* analogues).
enum {
  tcGetSelection = evUser + 340,
  tcSetSelection,
  tcnSelChange,
  tcSetStyle,       // wparam = TAB_STYLE_* flags
  tcSetImageStrip,   // lparam = bitmap_strip_t*; sets shared icon strip for all tabs
  tcSetTabIcon,      // wparam = tab_index; lparam = (void*)(intptr_t)icon_index in the strip; -1 = clear
};
#define TAB_CONTROL_HEIGHT 22
#define TAB_STYLE_ICONS_ONLY (1u << 0) // show tab icons without page-title labels

// Analogous to WinAPI CW_USEDEFAULT: pass as x or y to create_window() /
// create_window_from_form() to let the framework auto-position the window.
#define CW_USEDEFAULT  (-32768)

// Icon enumerations for UI controls
typedef enum {
  icon8_minus,
  icon8_collapse,
  icon8_maximize,
  icon8_dropdown,
  icon8_checkbox,
  icon8_editor_helmet,
  // Scrollbar arrow icons (indices 9–12) and resize-corner icon (index 13).
  // Pixels for these slots were added to icons.c at their respective positions.
  icon8_scroll_up    = 9,
  icon8_scroll_right = 10,
  icon8_scroll_down  = 11,
  icon8_scroll_left  = 12,
  icon8_resize_br    = 13,
  icon8_count,
} icon8_t;

// System color indices — analogous to WinAPI GetSysColor(nIndex).
// Access via get_sys_color(brXxx); change via set_sys_colors().
typedef enum {
  brTransparent          = 0,   // fully transparent / no fill
  brControlBg            = 1,   // dialog, panel, and control-face background
  brWindowDarkBg         = 2,   // dark secondary panel background
  brWorkspaceBg          = 3,   // document / canvas workspace area
  brActiveTitlebar       = 4,   // focused window title bar background
  brActiveTitlebarText   = 5,   // focused window title bar text
  brInactiveTitlebar     = 6,   // unfocused window title bar background
  brInactiveTitlebarText = 7,   // unfocused window title bar text
  brStatusbarBg          = 8,   // status bar background
  brLightEdge            = 9,   // highlight edge of beveled elements
  brDarkEdge             = 10,  // shadow edge of beveled elements
  brFlare                = 11,  // corner flare of beveled elements
  brAccent               = 12,  // focus, selection, and active-state accent
  brButtonInner          = 13,  // inner fill of button
  brButtonHover          = 14,  // button hover state
  brTextNormal           = 15,  // standard text
  brTextDisabled         = 16,  // disabled / inactive text
  brTextError            = 17,  // error message text
  brTextSuccess          = 18,  // success message text
  brBorderFocus          = 19,  // focused item dark outline
  brBorderActive         = 20,  // active item border
  brFolderText           = 21,  // folder entry text in file lists
  brColumnViewBg         = 22,  // report/icon column view background
  brModalOverlay         = 23,  // modal owner dimming overlay (ARGB with alpha)
  brToolbarForeground    = 24,  // toolbar icons, labels, and dropdown arrows
  brCount                = 25
} sys_color_idx_t;

// Runtime-accessible theme table (defined in user/theme.c).
extern uint32_t g_sys_colors[brCount];

// Inline color lookup — equivalent to WinAPI GetSysColor(nIndex).
static inline uint32_t get_sys_color(sys_color_idx_t idx) {
  return g_sys_colors[idx];
}

// Macros for creating rectangles
#define MAKERECT(X, Y, W, H) (&(irect16_t){X, Y, W, H})

// Macros for extracting DWORD parts
#ifndef LOWORD
# define LOWORD(l) ((uint16_t)(l & 0xFFFF))
#endif

#ifndef HIWORD
# define HIWORD(l) ((uint16_t)((l >> 16) & 0xFFFF))
#endif

#ifndef MAKEDWORD
# define MAKEDWORD(low, high) ((uint32_t)(((uint16_t)(low)) | ((uint32_t)((uint16_t)(high))) << 16))
#endif

#ifndef MAKEWPARAM
# define MAKEWPARAM(lo, hi) MAKEDWORD(lo, hi)
#endif

// Helper macros
#ifndef MAX
# define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


#endif
