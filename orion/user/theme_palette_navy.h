#ifndef __THEME_PALETTE_NAVY_H__
#define __THEME_PALETTE_NAVY_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Blue chrome and cyan selection for the Navy theme.
// Caption follows Win95/XP: active = accent, inactive = dull bar, not the face.
// Near-duplicate surfaces share one swatch. Menu, toolbar, status, and the
// document mat are NAVY_CHROME; inactive tabs sit one step under the face.
// WEB() packs CSS #rrggbb with R in the low byte.

#define NAVY_SHADOW     WEB(0x0D1B34)
#define NAVY_CHROME     WEB(0x142440)
#define NAVY_RECESS     WEB(0x1C3152)
#define NAVY_FACE       WEB(0x243D60)
#define NAVY_RAISED     WEB(0x294669)
#define NAVY_HOVER      WEB(0x365B85)
#define NAVY_LINE       WEB(0x456B96)
#define NAVY_HAIRLINE   WEB(0x658BB3)
#define NAVY_TEXT       WEB(0xF8FAFF)
#define NAVY_TEXT_MUTED WEB(0xA8B7CC)
#define NAVY_ACCENT     WEB(0x168CFA)
#define NAVY_FOCUS      WEB(0x6DDCFF)
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
  [brPanelDarker]          = NAVY_CHROME, \
  [brTextWarning]          = WEB(0xF2C14E), \
  [brTextInfo]             = WEB(0x7AB8FF), \
  [brTextSecondary]        = NAVY_TEXT_MUTED, \
  [brTextOnColor]          = 0xff101010, \
  [brSelectionTop]         = WEB(0x37C9FF), \
  [brSelectionBottom]      = WEB(0x0879EA), \
  [brPlasticNeutral]       = WEB(0xc8d2e0)

static const uint32_t k_theme_palette_navy[brCount]
  __attribute__((unused)) = { THEME_PALETTE_NAVY_INIT };

#endif
