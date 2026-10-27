#ifndef __THEME_PALETTE_AMBER_H__
#define __THEME_PALETTE_AMBER_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Business-application palette: white content wells (tables and fields),
// warm grey chrome, a graphite caption, and amber for focus, the default
// button and selection, which carries dark text. Fields share
// brColumnViewBg with lists; brLightEdge is the hairline and scrollbar thumb.

#define THEME_PALETTE_AMBER_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0xF7F7F5), \
  [brWindowDarkBg]         = WEB(0xECECE9), \
  [brWorkspaceBg]          = WEB(0xCFD2D6), \
  [brActiveTitlebar]       = WEB(0x3A3E44), \
  [brActiveTitlebarText]   = WEB(0xFFFFFF), \
  [brInactiveTitlebar]     = WEB(0xD9DADC), \
  [brInactiveTitlebarText] = WEB(0x707378), \
  [brStatusbarBg]          = WEB(0xEEEEEB), \
  [brLightEdge]            = WEB(0xBEBEB8), \
  [brDarkEdge]             = WEB(0xD3D3CE), \
  [brFlare]                = WEB(0xFFFFFF), \
  [brAccent]               = WEB(0xFAD03C), \
  [brButtonInner]          = WEB(0xEDEDEA), \
  [brButtonHover]          = WEB(0xE1E1DC), \
  [brTextNormal]           = WEB(0x1E1E1E), \
  [brTextDisabled]         = WEB(0xA3A3A0), \
  [brTextError]            = WEB(0xC0392B), \
  [brTextSuccess]          = WEB(0x2E7D32), \
  [brBorderFocus]          = WEB(0xD4A10A), \
  [brBorderActive]         = WEB(0xC6C6C0), \
  [brFolderText]           = WEB(0x8A6D00), \
  [brColumnViewBg]         = WEB(0xFFFFFF), \
  [brModalOverlay]         = 0x40000000, \
  [brToolbarForeground]    = WEB(0x3C3C3C), \
  [brPanelDark]            = WEB(0xE7E7E3), \
  [brPanelDarker]          = WEB(0xDEDEDA), \
  [brTabBarBg]             = WEB(0xE3E3DF), \
  [brTextWarning]          = WEB(0xB26A00), \
  [brTextInfo]             = WEB(0x1F5FA8), \
  [brTextSecondary]        = WEB(0x66666A), \
  [brTextOnColor]          = WEB(0x1A1A1A), \
  [brSelectionTop]         = WEB(0xFFF2BF), \
  [brSelectionBottom]      = WEB(0xFBE59A), \
  [brPlasticNeutral]       = WEB(0xC8C8C4), \
  [brAccentText]           = WEB(0x1A1A1A), \
  [brTooltipBackground]   = WEB(0xE7E7E3), \
  [brTooltipText]         = WEB(0x1E1E1E), \
  [brInfoBackground]      = WEB(0xFFFFE1), \
  [brInfoText]            = WEB(0x303025), \
  [brDesktopBg]            = WEB(0x29356B)

static const uint32_t k_theme_palette_amber[brCount]
  __attribute__((unused)) = { THEME_PALETTE_AMBER_INIT };

#endif
