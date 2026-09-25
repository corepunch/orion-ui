#ifndef __THEME_PALETTE_NAVY_H__
#define __THEME_PALETTE_NAVY_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Navy chrome + purple accent sampled from the Pencil Test reference artwork.
// Caption follows Win95/XP: active = accent, inactive = dull bar, not the face.
// WEB() packs CSS #rrggbb with R in the low byte.

#define THEME_PALETTE_NAVY_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0x2F3F58), \
  [brWindowDarkBg]         = WEB(0x283A53), \
  [brWorkspaceBg]          = WEB(0x203550), \
  [brActiveTitlebar]       = WEB(0x8270F7), \
  [brActiveTitlebarText]   = WEB(0xF8FAFF), \
  [brInactiveTitlebar]     = WEB(0x33445B), \
  [brInactiveTitlebarText] = WEB(0xA8B7CC), \
  [brStatusbarBg]          = WEB(0x243750), \
  [brLightEdge]            = WEB(0x657894), \
  [brDarkEdge]             = WEB(0x1A2A42), \
  [brFlare]                = WEB(0xF8FAFF), \
  [brAccent]               = WEB(0x8270F7), \
  [brButtonInner]          = WEB(0x33455F), \
  [brButtonHover]          = WEB(0x425473), \
  [brTextNormal]           = WEB(0xF8FAFF), \
  [brTextDisabled]         = WEB(0x9CACBF), \
  [brTextError]            = WEB(0xC42B1C), \
  [brTextSuccess]          = WEB(0x63C994), \
  [brBorderFocus]          = WEB(0xA090FF), \
  [brBorderActive]         = WEB(0x536882), \
  [brFolderText]           = WEB(0x8FC7FF), \
  [brColumnViewBg]         = WEB(0x33445B), \
  [brModalOverlay]         = (WEB(0x203550) & 0x00ffffffu) | 0x40000000u, \
  [brToolbarForeground]    = WEB(0xF8FAFF), \
  [brPanelDark]            = WEB(0x2B3D56), \
  [brPanelDarker]          = WEB(0x20324C)

static const uint32_t k_theme_palette_navy[brCount]
  __attribute__((unused)) = { THEME_PALETTE_NAVY_INIT };

#endif
