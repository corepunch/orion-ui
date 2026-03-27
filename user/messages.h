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
  kWindowMessageKeyDown,
  kWindowMessageKeyUp,
  kWindowMessageJoyButtonDown,
  kWindowMessageJoyButtonUp,
  kWindowMessageJoyAxisMotion,
  kWindowMessageStatusBar,
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
};

// Control notification messages
enum {
  kEditNotificationUpdate = 100,
  kButtonNotificationClicked,
  kComboBoxNotificationSelectionChange,
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
// Bitmap/image buttons are a separate window class (win_toolbar_button), not a flag on win_button.
// Use kButtonMessageSetImage on a win_toolbar_button window to assign an icon from a bitmap_strip_t.
#define BUTTON_PUSHLIKE     (1 << 13)
#define BUTTON_AUTORADIO    (1 << 14)

// Titlebar and toolbar dimensions
#define TITLEBAR_HEIGHT   12
#define TOOLBAR_HEIGHT    20
#define STATUSBAR_HEIGHT  12
#define RESIZE_HANDLE     8
#define BUTTON_HEIGHT     13
#define WINDOW_PADDING 4
#define LINE_PADDING 5
#define CONTROL_HEIGHT 10

// Control button dimensions
#define CONTROL_BUTTON_WIDTH    8
#define CONTROL_BUTTON_PADDING  2
#define TB_SPACING              18

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
