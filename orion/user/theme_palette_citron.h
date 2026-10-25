#ifndef __THEME_PALETTE_CITRON_H__
#define __THEME_PALETTE_CITRON_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Citron: Ledger's familiar yellow on cool, colour-neutral greys. White
// content wells, a light caption instead of a graphite bar, and lemon used
// only where 1C users expect it: the default button, the current section and
// the current row, all with dark ink. Tests whether the yellow or the warm
// greys made Amber read as a Linux desktop.

#define THEME_PALETTE_CITRON_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0xF7F8FA), \
  [brWindowDarkBg]         = WEB(0xEFF1F4), \
  [brWorkspaceBg]          = WEB(0xE4E7EB), \
  [brActiveTitlebar]       = WEB(0xEEF0F3), \
  [brActiveTitlebarText]   = WEB(0x16181D), \
  [brInactiveTitlebar]     = WEB(0xF4F5F7), \
  [brInactiveTitlebarText] = WEB(0x9499A3), \
  [brStatusbarBg]          = WEB(0xF1F3F5), \
  [brLightEdge]            = WEB(0xD3D7DE), \
  [brDarkEdge]             = WEB(0xE0E3E8), \
  [brFlare]                = WEB(0xFFFFFF), \
  [brAccent]               = WEB(0xFFD233), \
  [brButtonInner]          = WEB(0xF0F2F5), \
  [brButtonHover]          = WEB(0xE5E8ED), \
  [brTextNormal]           = WEB(0x16181D), \
  [brTextDisabled]         = WEB(0xA8ADB6), \
  [brTextError]            = WEB(0xD1352B), \
  [brTextSuccess]          = WEB(0x1E8E4E), \
  [brBorderFocus]          = WEB(0xE8B500), \
  [brBorderActive]         = WEB(0xC9CED6), \
  [brFolderText]           = WEB(0x8A6A00), \
  [brColumnViewBg]         = WEB(0xFFFFFF), \
  [brModalOverlay]         = 0x40000000, \
  [brToolbarForeground]    = WEB(0x3A3E46), \
  [brPanelDark]            = WEB(0xECEEF2), \
  [brPanelDarker]          = WEB(0xF1F3F6), \
  [brTabBarBg]             = WEB(0xE8EBEF), \
  [brTextWarning]          = WEB(0xB86E00), \
  [brTextInfo]             = WEB(0x2563C9), \
  [brTextSecondary]        = WEB(0x676C76), \
  [brTextOnColor]          = WEB(0x16181D), \
  [brSelectionTop]         = WEB(0xFFF3BF), \
  [brSelectionBottom]      = WEB(0xFFF3BF), \
  [brPlasticNeutral]       = WEB(0xC9CDD3), \
  [brAccentText]           = WEB(0x16181D), \
  [brDesktopBg]            = WEB(0x33363C)

static const uint32_t k_theme_palette_citron[brCount]
  __attribute__((unused)) = { THEME_PALETTE_CITRON_INIT };

#endif
