#ifndef __THEME_PALETTE_SAGE_H__
#define __THEME_PALETTE_SAGE_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Sage: neutral surfaces with a lime accent and forest ink, the pairing
// money apps use for "go". The default button and current section are lime
// with forest text; the current row is a pale lime wash.

#define THEME_PALETTE_SAGE_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0xF7F8F7), \
  [brWindowDarkBg]         = WEB(0xEEF0EE), \
  [brWorkspaceBg]          = WEB(0xE3E7E4), \
  [brActiveTitlebar]       = WEB(0xEEF0EE), \
  [brActiveTitlebarText]   = WEB(0x15201A), \
  [brInactiveTitlebar]     = WEB(0xF4F5F4), \
  [brInactiveTitlebarText] = WEB(0x949A96), \
  [brStatusbarBg]          = WEB(0xF1F3F1), \
  [brLightEdge]            = WEB(0xD2D8D4), \
  [brDarkEdge]             = WEB(0xDFE4E0), \
  [brFlare]                = WEB(0xFFFFFF), \
  [brAccent]               = WEB(0xA3E07A), \
  [brButtonInner]          = WEB(0xEFF2EF), \
  [brButtonHover]          = WEB(0xE3E8E4), \
  [brTextNormal]           = WEB(0x15201A), \
  [brTextDisabled]         = WEB(0xA7AEA9), \
  [brTextError]            = WEB(0xCC3A2E), \
  [brTextSuccess]          = WEB(0x1F7A4D), \
  [brBorderFocus]          = WEB(0x3F8F4A), \
  [brBorderActive]         = WEB(0xC7CEC9), \
  [brFolderText]           = WEB(0x2F6B3A), \
  [brColumnViewBg]         = WEB(0xFFFFFF), \
  [brModalOverlay]         = 0x40000000, \
  [brToolbarForeground]    = WEB(0x36423B), \
  [brPanelDark]            = WEB(0xECEFEC), \
  [brPanelDarker]          = WEB(0xF1F3F1), \
  [brTabBarBg]             = WEB(0xE7EBE8), \
  [brTextWarning]          = WEB(0xA86A00), \
  [brTextInfo]             = WEB(0x2B67B8), \
  [brTextSecondary]        = WEB(0x5F6A63), \
  [brTextOnColor]          = WEB(0x15201A), \
  [brSelectionTop]         = WEB(0xEAF6DE), \
  [brSelectionBottom]      = WEB(0xEAF6DE), \
  [brPlasticNeutral]       = WEB(0xC6CCC8), \
  [brAccentText]           = WEB(0x163300), \
  [brDesktopBg]            = WEB(0x1F2D25)

static const uint32_t k_theme_palette_sage[brCount]
  __attribute__((unused)) = { THEME_PALETTE_SAGE_INIT };

#endif
