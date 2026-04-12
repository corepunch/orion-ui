// System color theme table.
// Analogous to WinAPI GetSysColor / SetSysColors.
// Access colours via get_sys_color(kColorXxx); change them via set_sys_colors().

#include <stdint.h>
#include "messages.h"

uint32_t g_sys_colors[kColorCount] = {
  [kColorWindowBg]             = COLOR_PANEL_BG,
  [kColorWindowDarkBg]         = COLOR_PANEL_DARK_BG,
  [kColorWorkspaceBg]          = 0xff1e1e1e,   // darker than status bar — canvas workspace
  [kColorActiveTitlebar]       = 0xff1e5aa0,   // focused window: blue caption bar (dark theme)
  [kColorActiveTitlebarText]   = 0xffffffff,   // focused caption text: white
  [kColorInactiveTitlebar]     = COLOR_PANEL_DARK_BG,  // unfocused: same flat dark gray
  [kColorInactiveTitlebarText] = 0xff787878,   // unfocused caption text: medium gray
  [kColorStatusbarBg]          = COLOR_STATUSBAR_BG,
  [kColorLightEdge]            = COLOR_LIGHT_EDGE,
  [kColorDarkEdge]             = COLOR_DARK_EDGE,
  [kColorFlare]                = COLOR_FLARE,
  [kColorFocusRing]            = COLOR_FOCUSED,
  [kColorButtonBg]             = COLOR_BUTTON_BG,
  [kColorButtonInner]          = COLOR_BUTTON_INNER,
  [kColorButtonHover]          = COLOR_BUTTON_HOVER,
  [kColorTextNormal]           = COLOR_TEXT_NORMAL,
  [kColorTextDisabled]         = COLOR_TEXT_DISABLED,
  [kColorTextError]            = COLOR_TEXT_ERROR,
  [kColorTextSuccess]          = COLOR_TEXT_SUCCESS,
  [kColorBorderFocus]          = COLOR_BORDER_FOCUS,
  [kColorBorderActive]         = COLOR_BORDER_ACTIVE,
  [kColorFolderText]           = 0xffa0d000,
};

void set_sys_colors(int count, const int *indices, const uint32_t *colors) {
  for (int i = 0; i < count; i++) {
    if (indices[i] >= 0 && indices[i] < kColorCount) {
      g_sys_colors[indices[i]] = colors[i];
    }
  }
}
