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

// Base UI Colors
#define COLOR_PANEL_BG       0xff3c3c3c  // main panel or window background
#define COLOR_PANEL_DARK_BG  0xff2c2c2c  // main panel or window background
#define COLOR_STATUSBAR_BG   0xff2c2c2c  // status bar background
#define COLOR_LIGHT_EDGE     0xff7f7f7f  // top-left edge for beveled elements
#define COLOR_DARK_EDGE      0xff1a1a1a  // bottom-right edge for bevel
#define COLOR_FLARE          0xffcfcfcf  // top-left edge for beveled elements
#define COLOR_FOCUSED        0xff5EC4F3

// Additional UI Colors
#define COLOR_BUTTON_BG      0xff404040  // button background (unpressed)
#define COLOR_BUTTON_INNER   0xff505050  // inner fill of button
#define COLOR_BUTTON_HOVER   0xff5a5a5a  // slightly brighter for hover state
#define COLOR_TEXT_NORMAL    0xffc0c0c0  // standard text color
#define COLOR_TEXT_DISABLED  0xff808080  // for disabled/inactive text
#define COLOR_TEXT_ERROR     0xffff4444  // red text for errors
#define COLOR_TEXT_SUCCESS   0xff44ff44  // green text for success messages
#define COLOR_BORDER_FOCUS   0xff101010  // very dark outline for focused item
#define COLOR_BORDER_ACTIVE  0xff808080  // light gray for active border

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
