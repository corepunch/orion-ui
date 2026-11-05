#ifndef __THEME_PALETTE_METRO_H__
#define __THEME_PALETTE_METRO_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Metro: square dark surfaces and a bright blue accent for creative work.

#define THEME_PALETTE_METRO_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0x20262E), \
  [brWindowDarkBg]         = WEB(0x101318), \
  [brWorkspaceBg]          = WEB(0x101318), \
  [brActiveTitlebar]       = WEB(0x20262E), \
  [brActiveTitlebarText]   = WEB(0xFFFFFF), \
  [brInactiveTitlebar]     = WEB(0x101318), \
  [brInactiveTitlebarText] = WEB(0x5C6370), \
  [brStatusbarBg]          = WEB(0x101318), \
  [brLightEdge]            = WEB(0x343B44), \
  [brDarkEdge]             = WEB(0x101318), \
  [brFlare]                = WEB(0x3E4451), \
  [brAccent]               = WEB(0x0078D7), \
  [brButtonInner]          = WEB(0x28323C), \
  [brButtonHover]          = WEB(0x354455), \
  [brTextNormal]           = WEB(0xF1F5F8), \
  [brTextDisabled]         = WEB(0x717B85), \
  [brTextError]            = WEB(0xE06C75), \
  [brTextSuccess]          = WEB(0x98C379), \
  [brBorderFocus]          = WEB(0x0078D7), \
  [brBorderActive]         = WEB(0x343B44), \
  [brFolderText]           = WEB(0x61AFEF), \
  [brColumnViewBg]         = WEB(0x1A1E24), \
  [brModalOverlay]         = 0x60000000, \
  [brToolbarForeground]    = WEB(0x9DA5B4), \
  [brPanelDark]            = WEB(0x101318), \
  [brPanelDarker]          = WEB(0x101318), \
  [brTabBarBg]             = WEB(0x1D2025), \
  [brTextWarning]          = WEB(0xE5C07B), \
  [brTextInfo]             = WEB(0x61AFEF), \
  [brTextSecondary]        = WEB(0xA9B6C0), \
  [brTextOnColor]          = WEB(0xFFFFFF), \
  [brSelectionTop]         = WEB(0x0078D7), \
  [brSelectionBottom]      = WEB(0x0078D7), \
  [brAccentText]           = WEB(0xFFFFFF), \
  [brTooltipBackground]   = WEB(0x101318), \
  [brTooltipText]         = WEB(0xFFFFFF), \
  [brInfoBackground]      = WEB(0x28323C), \
  [brInfoText]            = WEB(0xC8CCD4), \
  [brDesktopBg]            = WEB(0x16181D)

static const uint32_t k_theme_palette_metro[brCount]
  __attribute__((unused)) = { THEME_PALETTE_METRO_INIT };

#endif
