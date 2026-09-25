#ifndef __THEME_PALETTE_NAVY_H__
#define __THEME_PALETTE_NAVY_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Navy chrome + purple accent sampled from the Pencil Test reference artwork.
// Caption follows Win95/XP: active = accent, inactive = dull bar, not the face.
// Near-duplicate surfaces share one swatch. Menu, toolbar, status, and the
// document mat are NAVY_CHROME; inactive tabs sit one step under the face.
// WEB() packs CSS #rrggbb with R in the low byte.

#define NAVY_SHADOW     WEB(0x1A2A42)
#define NAVY_CHROME     WEB(0x20324C)
#define NAVY_RECESS     WEB(0x283A53)
#define NAVY_FACE       WEB(0x2F3F58)
#define NAVY_RAISED     WEB(0x33445B)
#define NAVY_HOVER      WEB(0x425473)
#define NAVY_LINE       WEB(0x536882)
#define NAVY_HAIRLINE   WEB(0x657894)
#define NAVY_TEXT       WEB(0xF8FAFF)
#define NAVY_TEXT_MUTED WEB(0xA8B7CC)
#define NAVY_ACCENT     WEB(0x8270F7)
#define NAVY_FOCUS      WEB(0xA090FF)
#define NAVY_FOLDER     WEB(0x8FC7FF)
#define NAVY_ERROR      WEB(0xC42B1C)
#define NAVY_SUCCESS    WEB(0x63C994)

#define THEME_PALETTE_NAVY_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = NAVY_FACE, \
  [brWindowDarkBg]         = NAVY_CHROME, \
  [brWorkspaceBg]          = NAVY_CHROME, \
  [brActiveTitlebar]       = NAVY_ACCENT, \
  [brActiveTitlebarText]   = NAVY_TEXT, \
  [brInactiveTitlebar]     = NAVY_RAISED, \
  [brInactiveTitlebarText] = NAVY_TEXT_MUTED, \
  [brStatusbarBg]          = NAVY_CHROME, \
  [brLightEdge]            = NAVY_HAIRLINE, \
  [brDarkEdge]             = NAVY_SHADOW, \
  [brFlare]                = NAVY_TEXT, \
  [brAccent]               = NAVY_ACCENT, \
  [brButtonInner]          = NAVY_RAISED, \
  [brButtonHover]          = NAVY_HOVER, \
  [brTextNormal]           = NAVY_TEXT, \
  [brTextDisabled]         = NAVY_TEXT_MUTED, \
  [brTextError]            = NAVY_ERROR, \
  [brTextSuccess]          = NAVY_SUCCESS, \
  [brBorderFocus]          = NAVY_FOCUS, \
  [brBorderActive]         = NAVY_LINE, \
  [brFolderText]           = NAVY_FOLDER, \
  [brColumnViewBg]         = NAVY_RAISED, \
  [brModalOverlay]         = (NAVY_CHROME & 0x00ffffffu) | 0x40000000u, \
  [brToolbarForeground]    = NAVY_TEXT, \
  [brPanelDark]            = NAVY_RECESS, \
  [brPanelDarker]          = NAVY_CHROME

static const uint32_t k_theme_palette_navy[brCount]
  __attribute__((unused)) = { THEME_PALETTE_NAVY_INIT };

#endif
