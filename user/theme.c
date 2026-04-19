// System color theme table.
// Analogous to WinAPI GetSysColor / SetSysColors.
// Access colours via get_sys_color(kColorXxx); change them via set_sys_colors().

#include <stdint.h>
#include "messages.h"
#include "user.h"

uint32_t g_sys_colors[kColorCount] = {
  [kColorWindowBg]             = 0xff3c3c3c,   // main panel / window background
  [kColorWindowDarkBg]         = 0xff2c2c2c,   // dark secondary panel background
  [kColorWorkspaceBg]          = 0xff1e1e1e,   // darker than status bar — canvas workspace
  [kColorActiveTitlebar]       = 0xffa05a1e,   // focused window: blue caption bar (dark theme)
  [kColorActiveTitlebarText]   = 0xffffffff,   // focused caption text: white
  [kColorInactiveTitlebar]     = 0xff2c2c2c,   // unfocused: flat dark gray
  [kColorInactiveTitlebarText] = 0xff787878,   // unfocused caption text: medium gray
  [kColorStatusbarBg]          = 0xff2c2c2c,   // status bar background
  [kColorLightEdge]            = 0xff7f7f7f,   // top-left edge for beveled elements
  [kColorDarkEdge]             = 0xff1a1a1a,   // bottom-right edge for bevel
  [kColorFlare]                = 0xffcfcfcf,   // corner flare for beveled elements
  [kColorFocusRing]            = 0xff5EC4F3,   // keyboard-focus ring
  [kColorButtonBg]             = 0xff404040,   // button background (unpressed)
  [kColorButtonInner]          = 0xff505050,   // inner fill of button
  [kColorButtonHover]          = 0xff5a5a5a,   // slightly brighter for hover state
  [kColorTextNormal]           = 0xffc0c0c0,   // standard text color
  [kColorTextDisabled]         = 0xff808080,   // for disabled/inactive text
  [kColorTextError]            = 0xffff4444,   // red text for errors
  [kColorTextSuccess]          = 0xff44ff44,   // green text for success messages
  [kColorBorderFocus]          = 0xff101010,   // very dark outline for focused item
  [kColorBorderActive]         = 0xff808080,   // light gray for active border
  [kColorFolderText]           = 0xffa0d000,   // folder entry text in file lists
  [kColorColumnViewBg]         = 0xff544e47,   // blue-gray for report/icon column views
};

void set_sys_colors(int count, const int *indices, const uint32_t *colors) {
  for (int i = 0; i < count; i++) {
    if (indices[i] >= 0 && indices[i] < kColorCount) {
      g_sys_colors[indices[i]] = colors[i];
    }
  }
  if (g_ui_runtime.running) {
    post_message((window_t*)1, kWindowMessageRefreshStencil, 0, NULL);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (w->visible) invalidate_window(w);
    }
  }
}
