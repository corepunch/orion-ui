#ifndef __UI_MESSAGES_H__
#define __UI_MESSAGES_H__

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
  evMouseActivate,
  evActivate,
  evSetFocus,
  evKillFocus,
  evHitTest,
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
  evUser = 1000
};

// Control messages
enum {
  btnSetCheck = evUser,
  btnGetCheck,
  btnSetImage,       // wparam = icon index (iBitmap); lparam = bitmap_strip_t*
  cbAddString,
  cbGetCurrentSelection,
  cbSetCurrentSelection,
  cbGetListBoxText,
  cbClear,            // clear all items and reset title
  sbAddWindow,
  tbButtonClick,
  tbSetStrip,         // wparam=0, lparam=bitmap_strip_t* (or NULL to clear)
  tbSetActiveButton,  // wparam=ident of button to mark active
  sbSetInfo,        // lparam = scrollbar_info_t*
  sbGetPos,         // returns current scroll position
  tbSetButtonSize,    // wparam=square button size in pixels (0 resets to TB_SPACING)
  tbLoadStrip,        // wparam=icon tile size in px (square); lparam=const char* path to PNG
  tbSetItems,         // wparam=count; lparam=toolbar_item_t* — create real child windows
  // Multiline text edit messages (analogous to WM_GETTEXT / WM_SETTEXT)
  edGetText,        // wparam=buf_size; lparam=char* dst → copies text, returns length
  edSetText,        // wparam=0; lparam=const char* src → replaces text
  // List (popup) messages
  lstSetItem,             // wparam=item index to pre-select in the dropdown list
  // Toolbox control messages (commctl/toolbox.c)
  bxSetItems,         // wparam=count; lparam=toolbox_item_t[] — copy item list
  bxSetActiveItem,    // wparam=ident (-1 = clear active)
  bxSetStrip,         // wparam=0; lparam=bitmap_strip_t* (NULL=clear) — external strip
  bxSetButtonSize,    // wparam=size in px (0 = reset to TOOLBOX_BTN_SIZE)
  bxLoadStrip,        // wparam=icon_w (square tiles); lparam=const char* path — load PNG
  bxSetIconTintBrush, // wparam=br* index (e.g., brTextNormal), -1 disables tint
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
  // Create or replace the sidebar child window for a WINDOW_SIDEBAR window.
  // wparam = sidebar width in pixels (0 uses SIDEBAR_DEFAULT_WIDTH).
  // lparam = winproc_t — the window procedure for the sidebar content window.
  // The framework creates a WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_VSCROLL |
  // WINDOW_NOTRAYBUTTON child at (0, 0) and stores it in win->sidebar_child.
  sbSetContent,
};

// Control notification messages
enum {
  edUpdate = 100,
  btnClicked,
  cbSelectionChange,
  sbChanged,  // wparam: MAKEDWORD(scrollbar_id, sbChanged); lparam: (void*)(intptr_t)new_pos
  bxClicked,    // sent via evCommand: MAKEDWORD(ident, bxClicked)

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

// Toolbox item descriptor — one button in a win_toolbox 2-column grid.
// Set via bxSetItems.  icon is a sysicon_* value (>= SYSICON_BASE)
// or a tile index into the strip set with bxSetStrip /
// bxLoadStrip.
typedef struct {
  int ident;  // command identifier echoed in bxClicked
  int icon;   // strip tile index (0-based), or sysicon_* value (>= SYSICON_BASE)
} toolbox_item_t;

// Toolbox layout constants.
// TOOLBOX_COLS is always 2 — toolboxes are a fixed-width 2-column grid.
// TOOLBOX_BTN_SIZE is intentionally set equal to TB_SPACING (22 px) so that
// toolbox buttons have the same square size as toolbar buttons.  If you need
// a different size, override per-window with bxSetButtonSize.
// Window width  = TOOLBOX_COLS * TOOLBOX_BTN_SIZE = 44 px.
// Window height = TITLEBAR_HEIGHT + ceil(n/2) * TOOLBOX_BTN_SIZE.
#define TOOLBOX_COLS      2
#define TOOLBOX_BTN_SIZE  TB_SPACING  // 22 px (= TB_SPACING by design)

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
// A window with WINDOW_SIDEBAR has a fixed-width panel anchored to the left of
// its client area.  The panel is created via sbSetContent and participates in
// the normal child-window paint/event dispatch.  A 1-pixel vertical separator
// is drawn between the sidebar and the content area during evNCPaint.
#define WINDOW_SIDEBAR      (1 << 16)

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
#define SCROLLBAR_WIDTH  12
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
// FONT_SIZE (FONT_SYSTEM/ChiKareGo2): 12 at UI_WINDOW_SCALE==1, 8 at scale>=2.
// FONT_SIZE_SMALL (FONT_SMALL/Geneva9): 12 at UI_WINDOW_SCALE==1, 8 at scale>=2.
// Content heights (list rows, column-view entries) use FONT_SIZE_SMALL;
// see commctl/columnview.h.  All values are usable in static initializers.
#define TITLEBAR_HEIGHT   (FONT_SIZE + 4)
#define TOOLBAR_HEIGHT    22
#define STATUSBAR_HEIGHT  (FONT_SIZE + 4)
// Default width of a WINDOW_SIDEBAR panel in logical pixels.
#define SIDEBAR_DEFAULT_WIDTH  180
// Resize handle matches SCROLLBAR_WIDTH so the scrollbar corner cell is fully
// interactive as a resize drag grip (same as Windows 1.0/2.0 behaviour).
#define RESIZE_HANDLE     SCROLLBAR_WIDTH
#define BUTTON_HEIGHT     18
#define WINDOW_PADDING 4
#define LINE_PADDING 5
#define CONTROL_HEIGHT 14

// Control button dimensions
#define CONTROL_BUTTON_WIDTH    8
#define CONTROL_BUTTON_PADDING  2
#define TB_SPACING              TOOLBAR_HEIGHT  // equals TOOLBAR_HEIGHT so toolbar buttons are square
#define TOOLBAR_PADDING         2               // pixels of margin between toolbar border and button area (all sides)
#define TOOLBAR_SPACING         1               // minimal pixel gap between consecutive toolbar elements
#define TOOLBAR_SPACING_GAP_WIDTH  4            // pixels of gap inserted by a TOOLBAR_ITEM_SPACER entry
#define TOOLBAR_BEVEL_WIDTH     1               // width of the bevel border drawn around the toolbar button area (each side)
#define TOOLBAR_LABEL_PADDING           8       // horizontal padding added to auto-computed label width (left+right)
#define TOOLBAR_COMBOBOX_DEFAULT_WIDTH_MULT  3  // default combobox width = button_size * this multiplier
#define TOOLBAR_BUTTON_FLAG_ACTIVE   (1u << 0)
#define TOOLBAR_BUTTON_FLAG_PRESSED  (1u << 1)

// Toolbar item types used with tbSetItems.
typedef enum {
  TOOLBAR_ITEM_BUTTON    = 0,  // icon-only button (win_toolbar_button)
  TOOLBAR_ITEM_LABEL     = 1,  // static text label (win_label)
  TOOLBAR_ITEM_COMBOBOX  = 2,  // drop-down combobox (win_combobox)
  TOOLBAR_ITEM_TEXTEDIT  = 3,  // single-line text input (win_textedit)
  TOOLBAR_ITEM_SEPARATOR = 4,  // narrow visual separator (no interaction)
  TOOLBAR_ITEM_SPACER    = 5,  // invisible gap (no child window created)
} toolbar_item_type_t;

// Descriptor for a single toolbar item (used with tbSetItems).
typedef struct {
  toolbar_item_type_t type;   // item type
  int                 ident;  // command ID / button identifier
  int                 icon;   // BUTTON: sysicon_* value or custom strip index; -1 = uses sysicon_missing
  int                 w;      // explicit width in pixels (0 = automatic)
  uint32_t            flags;  // extra style flags (BUTTON_PUSHLIKE, BUTTON_AUTORADIO, …)
  const char         *text;   // label text, or combobox/textedit initial text
} toolbar_item_t;

// Analogous to WinAPI CW_USEDEFAULT: pass as x or y to create_window() /
// create_window_from_form() to let the framework auto-position the window.
#define CW_USEDEFAULT  (-32768)

// Scroll and interaction constants
#define SCROLL_SENSITIVITY      3

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
  brWindowBg             = 0,   // general panel / dialog background
  brWindowDarkBg         = 1,   // dark secondary panel background
  brWorkspaceBg          = 2,   // document / canvas workspace area
  brActiveTitlebar       = 3,   // focused window title bar background
  brActiveTitlebarText   = 4,   // focused window title bar text
  brInactiveTitlebar     = 5,   // unfocused window title bar background
  brInactiveTitlebarText = 6,   // unfocused window title bar text
  brStatusbarBg          = 7,   // status bar background
  brLightEdge            = 8,   // highlight edge of beveled elements
  brDarkEdge             = 9,   // shadow edge of beveled elements
  brFlare                = 10,  // corner flare of beveled elements
  brFocusRing            = 11,  // keyboard focus highlight ring
  brButtonBg             = 12,  // button background (unpressed)
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
  brCount                = 24
} sys_color_idx_t;

// Runtime-accessible theme table (defined in user/theme.c).
extern uint32_t g_sys_colors[brCount];

// Inline color lookup — equivalent to WinAPI GetSysColor(nIndex).
static inline uint32_t get_sys_color(sys_color_idx_t idx) {
  return g_sys_colors[idx];
}

// Macros for creating rectangles
#define MAKERECT(X, Y, W, H) (&(rect_t){X, Y, W, H})

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

// Helper macros
#ifndef MAX
# define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


#endif
