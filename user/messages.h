#ifndef __UI_MESSAGES_H__
#define __UI_MESSAGES_H__

// Window messages
enum {
  kWindowMessageCreate,
  kWindowMessageDestroy,
  kWindowMessageShowWindow,
  kWindowMessageNonClientPaint,
  kWindowMessageNonClientLeftButtonUp,
  kWindowMessagePaint,
  kWindowMessageRefreshStencil,
  kWindowMessagePaintStencil,
  kWindowMessageMouseActivate,
  kWindowMessageActivate,
  kWindowMessageSetFocus,
  kWindowMessageKillFocus,
  kWindowMessageHitTest,
  kWindowMessageCommand,
  kWindowMessageTextInput,
  kWindowMessageWheel,
  kWindowMessageMouseMove,
  kWindowMessageMouseLeave,
  kWindowMessageLeftButtonDown,
  kWindowMessageLeftButtonUp,
  kWindowMessageLeftButtonDoubleClick,
  kWindowMessageRightButtonDown,
  kWindowMessageRightButtonUp,
  kWindowMessageResize,
  kWindowMessageDisplayChange,
  kWindowMessageKeyDown,
  kWindowMessageKeyUp,
  kWindowMessageJoyButtonDown,
  kWindowMessageJoyButtonUp,
  kWindowMessageJoyAxisMotion,
  kWindowMessageStatusBar,
  // Sent to a window when its built-in horizontal/vertical scrollbar position
  // changes (analogous to WinAPI WM_HSCROLL / WM_VSCROLL).
  // wparam = new scroll position; lparam = NULL.
  kWindowMessageHScroll,
  kWindowMessageVScroll,
  // Sent when the user clicks the close (X) button on a non-dialog window.
  // Analogous to WM_CLOSE in WinAPI.
  // Return true  to cancel the close (e.g. show "unsaved changes?" dialog).
  // Return false to allow the default action (hide the window).
  kWindowMessageClose,
  kWindowMessageUser = 1000
};

// Control messages
enum {
  kButtonMessageSetCheck = kWindowMessageUser,
  kButtonMessageGetCheck,
  kButtonMessageSetImage,       // wparam = icon index (iBitmap); lparam = bitmap_strip_t*
  kComboBoxMessageAddString,
  kComboBoxMessageGetCurrentSelection,
  kComboBoxMessageSetCurrentSelection,
  kComboBoxMessageGetListBoxText,
  kStatusBarMessageAddWindow,
  kToolBarMessageAddButtons,
  kToolBarMessageButtonClick,
  kToolBarMessageSetStrip,         // wparam=0, lparam=bitmap_strip_t* (or NULL to clear)
  kToolBarMessageSetActiveButton,  // wparam=ident of button to mark active
  kScrollBarMessageSetInfo,        // lparam = scrollbar_info_t*
  kScrollBarMessageGetPos,         // returns current scroll position
  kToolBarMessageSetButtonSize,    // wparam=square button size in pixels (0 resets to TB_SPACING)
};

// Control notification messages
enum {
  kEditNotificationUpdate = 100,
  kButtonNotificationClicked,
  kComboBoxNotificationSelectionChange,
  kScrollBarNotificationChanged,  // wparam: MAKEDWORD(scrollbar_id, kScrollBarNotificationChanged); lparam: (void*)(intptr_t)new_pos
};

// Button state
enum {
  kButtonStateUnchecked,
  kButtonStateChecked
};

// WM_ACTIVATE state codes (wparam for kWindowMessageActivate)
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
// Use kButtonMessageSetImage on a win_toolbar_button window to assign an icon from a bitmap_strip_t.
#define BUTTON_PUSHLIKE     (1 << 13)
#define BUTTON_AUTORADIO    (1 << 14)
#define BUTTON_DEFAULT      (1 << 15)

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

// Titlebar and toolbar dimensions
#define TITLEBAR_HEIGHT   12
#define TOOLBAR_HEIGHT    22
#define STATUSBAR_HEIGHT  12
// Resize handle matches SCROLLBAR_WIDTH so the scrollbar corner cell is fully
// interactive as a resize drag grip (same as Windows 1.0/2.0 behaviour).
#define RESIZE_HANDLE     SCROLLBAR_WIDTH
#define BUTTON_HEIGHT     13
#define WINDOW_PADDING 4
#define LINE_PADDING 5
#define CONTROL_HEIGHT 10

// Control button dimensions
#define CONTROL_BUTTON_WIDTH    8
#define CONTROL_BUTTON_PADDING  2
#define TB_SPACING              TOOLBAR_HEIGHT  // equals TOOLBAR_HEIGHT so toolbar buttons are square

// Scroll and interaction constants
#define SCROLL_SENSITIVITY      5

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
// Access via get_sys_color(kColorXxx); change via set_sys_colors().
typedef enum {
  kColorWindowBg             = 0,   // general panel / dialog background
  kColorWindowDarkBg         = 1,   // dark secondary panel background
  kColorWorkspaceBg          = 2,   // document / canvas workspace area
  kColorActiveTitlebar       = 3,   // focused window title bar background
  kColorActiveTitlebarText   = 4,   // focused window title bar text
  kColorInactiveTitlebar     = 5,   // unfocused window title bar background
  kColorInactiveTitlebarText = 6,   // unfocused window title bar text
  kColorStatusbarBg          = 7,   // status bar background
  kColorLightEdge            = 8,   // highlight edge of beveled elements
  kColorDarkEdge             = 9,   // shadow edge of beveled elements
  kColorFlare                = 10,  // corner flare of beveled elements
  kColorFocusRing            = 11,  // keyboard focus highlight ring
  kColorButtonBg             = 12,  // button background (unpressed)
  kColorButtonInner          = 13,  // inner fill of button
  kColorButtonHover          = 14,  // button hover state
  kColorTextNormal           = 15,  // standard text
  kColorTextDisabled         = 16,  // disabled / inactive text
  kColorTextError            = 17,  // error message text
  kColorTextSuccess          = 18,  // success message text
  kColorBorderFocus          = 19,  // focused item dark outline
  kColorBorderActive         = 20,  // active item border
  kColorFolderText           = 21,  // folder entry text in file lists
  kColorCount                = 22
} sys_color_idx_t;

// Runtime-accessible theme table (defined in user/theme.c).
extern uint32_t g_sys_colors[kColorCount];

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
