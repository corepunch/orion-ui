#ifndef __THEME_PALETTE_PORCELAIN_DARK_H__
#define __THEME_PALETTE_PORCELAIN_DARK_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Porcelain Dark: Porcelain's dark counterpart. Graphite chrome, near-black
// wells, the sidebar a step darker than the face; the same indigo selection
// with white text. Status colours are lifted for contrast on dark surfaces.

#define THEME_PALETTE_PORCELAIN_DARK_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0x1E1F24), \
  [brWindowDarkBg]         = WEB(0x18191D), \
  [brWorkspaceBg]          = WEB(0x121316), \
  [brActiveTitlebar]       = WEB(0x232429), \
  [brActiveTitlebarText]   = WEB(0xE8E9ED), \
  [brInactiveTitlebar]     = WEB(0x1C1D21), \
  [brInactiveTitlebarText] = WEB(0x71757F), \
  [brStatusbarBg]          = WEB(0x1A1B1F), \
  [brLightEdge]            = WEB(0x3A3D46), \
  [brDarkEdge]             = WEB(0x2A2C33), \
  [brFlare]                = WEB(0x3E414A), \
  [brAccent]               = WEB(0x6E7BF2), \
  [brButtonInner]          = WEB(0x2A2B31), \
  [brButtonHover]          = WEB(0x31333A), \
  [brTextNormal]           = WEB(0xE8E9ED), \
  [brTextDisabled]         = WEB(0x60646E), \
  [brTextError]            = WEB(0xFF6B5E), \
  [brTextSuccess]          = WEB(0x4CC38A), \
  [brBorderFocus]          = WEB(0x8A95FF), \
  [brBorderActive]         = WEB(0x3E414A), \
  [brFolderText]           = WEB(0x9AA4FF), \
  [brColumnViewBg]         = WEB(0x16171B), \
  [brModalOverlay]         = 0x60000000, \
  [brToolbarForeground]    = WEB(0xCDD0D8), \
  [brPanelDark]            = WEB(0x1A1B20), \
  [brPanelDarker]          = WEB(0x141518), \
  [brTabBarBg]             = WEB(0x17181C), \
  [brTextWarning]          = WEB(0xF2B24C), \
  [brTextInfo]             = WEB(0x6FA8FF), \
  [brTextSecondary]        = WEB(0x9DA1AB), \
  [brTextOnColor]          = WEB(0x101114), \
  [brSelectionTop]         = WEB(0x5563DC), \
  [brSelectionBottom]      = WEB(0x4C5BD4), \
  [brPlasticNeutral]       = WEB(0x5A5D66), \
  [brAccentText]           = WEB(0xFFFFFF), \
  [brTooltipBackground]   = WEB(0x1E1F24), \
  [brTooltipText]         = WEB(0xFFFFFF), \
  [brInfoBackground]      = WEB(0x403B23), \
  [brInfoText]            = WEB(0xFFF4BD), \
  [brDesktopBg]            = WEB(0x2B2E36)

static const uint32_t k_theme_palette_porcelain_dark[brCount]
  __attribute__((unused)) = { THEME_PALETTE_PORCELAIN_DARK_INIT };

#endif
