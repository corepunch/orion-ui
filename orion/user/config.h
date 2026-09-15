// user/config.h — Tunable framework parameters
//
// This file contains configuration constants that users may want to adjust
// for their specific application needs or preferences.

#ifndef __UI_CONFIG_H__
#define __UI_CONFIG_H__

// ──────────────────────────────────────────────────────────────────────────
// Input and Scrolling
// ──────────────────────────────────────────────────────────────────────────

// Mouse wheel scroll multiplier (higher = more scroll distance per wheel tick)
// Values are multiplied by this before being passed to windows via evWheel.
// Recommended range: 3-10
#define SCROLL_SENSITIVITY      5

// Double-click time window in milliseconds
// Two clicks within this time are considered a double-click
#define DOUBLE_CLICK_MS         500u

// ──────────────────────────────────────────────────────────────────────────
// Window System
// ──────────────────────────────────────────────────────────────────────────

// Maximum number of registered window classes (window procedures)
#define MAX_WINDOW_CLASSES      256

// Window padding (internal spacing for containers)
#define WINDOW_PADDING          4

// ──────────────────────────────────────────────────────────────────────────
// Layout System
// ──────────────────────────────────────────────────────────────────────────

// Default spacing between auto-layout children (stacks, grids)
#define DEFAULT_LAYOUT_SPACING  8

// Minimum content size before scrollbars appear
#define MIN_SCROLL_CONTENT      20

// ──────────────────────────────────────────────────────────────────────────
// Text Rendering
// ──────────────────────────────────────────────────────────────────────────

// Maximum length for window titles
#define MAX_WINDOW_TITLE        256

// Maximum length for control text (buttons, labels, etc.)
#define MAX_CONTROL_TEXT        512

// ──────────────────────────────────────────────────────────────────────────
// Controls: Common
// ──────────────────────────────────────────────────────────────────────────

// Button horizontal padding (space between text and edges)
#define BUTTON_PADDING          8

// Textedit horizontal padding (between frame and text)
#define TEXTEDIT_PADDING_HORZ   4

// Textedit vertical padding (between frame and text)
#define TEXTEDIT_PADDING_VERT   1

// Checkbox box size (width and height of the checkbox square)
#define CHECKBOX_BOX_SIZE       13

// Checkbox focus ring padding
#define CHECKBOX_FOCUS_PAD      2

// ──────────────────────────────────────────────────────────────────────────
// Controls: Lists and Tables
// ──────────────────────────────────────────────────────────────────────────

// Maximum items in a list/combobox
#define MAX_LIST_ITEMS          256

// Maximum rows shown at once in a combobox dropdown
#define COMBOBOX_DROPDOWN_MAX_VISIBLE 8

// Maximum columns in a reportview (table)
#define MAX_REPORTVIEW_COLUMNS  16

// Reportview entry height (data rows)
#define REPORTVIEW_ENTRY_HEIGHT (FONT_SIZE_SMALL + 5)

// Reportview header height
#define REPORTVIEW_HEADER_HEIGHT (FONT_SIZE + 6)

// Reportview window padding
#define REPORTVIEW_WIN_PADDING  4

// Large icon view padding (outer grid margin)
#define LARGE_ICON_PAD          8

// Large icon view top padding (space above icon in cell)
#define LARGE_ICON_TOP_PAD      4

// Large icon view label gap (between icon bottom and label top)
#define LARGE_ICON_LABEL_GAP    4

// Large icon view bottom padding (space below label in cell)
#define LARGE_ICON_BOT_PAD      6

// ──────────────────────────────────────────────────────────────────────────
// Controls: Console/Messages
// ──────────────────────────────────────────────────────────────────────────

// Maximum number of console messages to keep in history
#define MAX_CONSOLE_MESSAGES    32

// How long to display a console message (milliseconds)
#define MESSAGE_DISPLAY_TIME    5000

// Fade out duration for console messages (milliseconds)
#define MESSAGE_FADE_TIME       1000

// Maximum console message length
#define MAX_MESSAGE_LENGTH      256

// Maximum number of console lines to display at once
#define MAX_CONSOLE_LINES       10

// ──────────────────────────────────────────────────────────────────────────
// Controls: Menu
// ──────────────────────────────────────────────────────────────────────────

// Menu geometry follows the more generous spacing of native desktop menus.
#define MENU_SEP_H              9

// Text-to-capsule padding, shared by menu-bar labels and popup items.
#define MENU_CAPSULE_PAD        12
#define MENU_CAPSULE_INSET      4
#define MENU_SIDE_PAD           (1 + MENU_CAPSULE_INSET + MENU_CAPSULE_PAD)

// Menu minimum popup width
#define MENU_MIN_W              180

// Menu top-level label padding
#define MENU_LABEL_PAD          16

// Menu gap between label and right-aligned hotkey
#define MENU_HOTKEY_GAP         16

// Menu vertical padding (above first/below last item)
#define MENU_START_Y            5

// ──────────────────────────────────────────────────────────────────────────
// Controls: Slider
// ──────────────────────────────────────────────────────────────────────────

// Maximum number of handles on a slider
#define SLIDER_MAX_HANDLES      4

// Minimum slider thumb width
#define SLIDER_MIN_THUMB_W      7

// Slider track padding
#define SLIDER_TRACK_PAD        8

// Slider bar vertical position
#define SLIDER_BAR_Y            4

// Slider bar height
#define SLIDER_BAR_H            8

// Slider handle vertical position
#define SLIDER_HANDLE_Y         8

// ──────────────────────────────────────────────────────────────────────────
// File Picker
// ──────────────────────────────────────────────────────────────────────────

// File picker list dimensions
#define FP_LIST_W               320
#define FP_LIST_H               160

// File picker padding
#define FP_PAD                  4

// File picker label column width ("File:", "Filter:")
#define FP_LABEL_W              38

// File picker button width
#define FP_BTN_W                50

// File picker row gap
#define FP_ROW_GAP              4

// Maximum number of file type filters
#define FP_MAX_FILTERS          16

// Maximum breadcrumb depth in location combobox
#define FP_MAX_LOC_DEPTH        16

#endif // __UI_CONFIG_H__
