// System color theme table.
// Analogous to WinAPI GetSysColor / SetSysColors.
// Access colours via get_sys_color(brXxx); change them via set_sys_colors().

#include <stdint.h>
#include "messages.h"
#include "user.h"

uint32_t g_sys_colors[brCount] = {
  [brWindowBg]             = 0xff3c3c3c,   // main panel / window background
  [brWindowDarkBg]         = 0xff2c2c2c,   // dark secondary panel background
  [brWorkspaceBg]          = 0xff1e1e1e,   // darker than status bar — canvas workspace
  [brActiveTitlebar]       = 0xffa05a1e,   // focused window: blue caption bar (dark theme)
  [brActiveTitlebarText]   = 0xffffffff,   // focused caption text: white
  [brInactiveTitlebar]     = 0xff4a4a4a,   // unfocused: slightly brighter than content, Win95-style separation
  [brInactiveTitlebarText] = 0xff787878,   // unfocused caption text: medium gray
  [brStatusbarBg]          = 0xff2c2c2c,   // status bar background
  [brLightEdge]            = 0xff7f7f7f,   // top-left edge for beveled elements
  [brDarkEdge]             = 0xff1a1a1a,   // bottom-right edge for bevel
  [brFlare]                = 0xffcfcfcf,   // corner flare for beveled elements
  [brFocusRing]            = 0xff5EC4F3,   // keyboard-focus ring
  [brButtonBg]             = 0xff404040,   // button background (unpressed)
  [brButtonInner]          = 0xff505050,   // inner fill of button
  [brButtonHover]          = 0xff5a5a5a,   // slightly brighter for hover state
  [brTextNormal]           = 0xffc0c0c0,   // standard text color
  [brTextDisabled]         = 0xff808080,   // for disabled/inactive text
  [brTextError]            = 0xffff4444,   // red text for errors
  [brTextSuccess]          = 0xff44ff44,   // green text for success messages
  [brBorderFocus]          = 0xff101010,   // very dark outline for focused item
  [brBorderActive]         = 0xff808080,   // light gray for active border
  [brFolderText]           = 0xffa0d000,   // folder entry text in file lists
  [brColumnViewBg]         = 0xff544e47,   // blue-gray for report/icon column views
  [brModalOverlay]         = 0x40402000,   // modal owner dim overlay (semi-transparent)
};

void set_sys_colors(int count, const int *indices, const uint32_t *colors) {
  for (int i = 0; i < count; i++) {
    if (indices[i] >= 0 && indices[i] < brCount) {
      g_sys_colors[indices[i]] = colors[i];
    }
  }
  if (g_ui_runtime.running) {
    post_message((window_t*)1, evRefreshStencil, 0, NULL);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (w->visible) invalidate_window(w);
    }
  }
}
