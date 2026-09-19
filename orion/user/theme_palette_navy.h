#ifndef __THEME_PALETTE_NAVY_H__
#define __THEME_PALETTE_NAVY_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Navy chrome + purple accent (Image Editor / Pencil Test standalone).
// Caption follows Win95/XP: active = accent, inactive = dull bar, not the face.
// WEB() packs CSS #rrggbb with R in the low byte.

#define THEME_PALETTE_NAVY_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0x17243B), \
  [brWindowDarkBg]         = WEB(0x1A2942), \
  [brWorkspaceBg]          = WEB(0x17243B), \
  [brActiveTitlebar]       = WEB(0x7357F6), \
  [brActiveTitlebarText]   = WEB(0xF6F8FF), \
  [brInactiveTitlebar]     = WEB(0x3A4558), \
  [brInactiveTitlebarText] = WEB(0x647089), \
  [brStatusbarBg]          = WEB(0x19273E), \
  [brLightEdge]            = WEB(0x4A5C7A), \
  [brDarkEdge]             = WEB(0x2A3954), \
  [brFlare]                = WEB(0xF8FAFF), \
  [brAccent]               = WEB(0x7357F6), \
  [brButtonInner]          = WEB(0x1B2942), \
  [brButtonHover]          = WEB(0x243552), \
  [brTextNormal]           = WEB(0xF6F8FF), \
  [brTextDisabled]         = WEB(0x647089), \
  [brTextError]            = WEB(0xC42B1C), \
  [brTextSuccess]          = WEB(0x63C994), \
  [brBorderFocus]          = WEB(0x8B70FF), \
  [brBorderActive]         = WEB(0x2A3954), \
  [brFolderText]           = WEB(0x4C91F5), \
  [brColumnViewBg]         = WEB(0x1A2942), \
  [brModalOverlay]         = (WEB(0x17243B) & 0x00ffffffu) | 0x40000000u, \
  [brToolbarForeground]    = WEB(0xF6F8FF), \
  [brPanelDark]            = WEB(0x121C30), \
  [brPanelDarker]          = WEB(0x0C1424)

static const uint32_t k_theme_palette_navy[brCount]
  __attribute__((unused)) = { THEME_PALETTE_NAVY_INIT };

#endif
