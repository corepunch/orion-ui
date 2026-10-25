#ifndef __THEME_PALETTE_MIDNIGHT_H__
#define __THEME_PALETTE_MIDNIGHT_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Midnight: a low-glare dark palette for long sessions. Near-black wells,
// graphite chrome, violet accent; the current row is a muted violet band
// with white text. Status colours are lifted for contrast on dark.

#define THEME_PALETTE_MIDNIGHT_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0x1C1D22), \
  [brWindowDarkBg]         = WEB(0x16171B), \
  [brWorkspaceBg]          = WEB(0x101114), \
  [brActiveTitlebar]       = WEB(0x202127), \
  [brActiveTitlebarText]   = WEB(0xE7E8EC), \
  [brInactiveTitlebar]     = WEB(0x1A1B1F), \
  [brInactiveTitlebarText] = WEB(0x6E727C), \
  [brStatusbarBg]          = WEB(0x18191D), \
  [brLightEdge]            = WEB(0x34363E), \
  [brDarkEdge]             = WEB(0x26282E), \
  [brFlare]                = WEB(0x3A3C44), \
  [brAccent]               = WEB(0x7B6EF6), \
  [brButtonInner]          = WEB(0x25262C), \
  [brButtonHover]          = WEB(0x2C2E35), \
  [brTextNormal]           = WEB(0xE6E7EB), \
  [brTextDisabled]         = WEB(0x5E626B), \
  [brTextError]            = WEB(0xFF6B5E), \
  [brTextSuccess]          = WEB(0x4CC38A), \
  [brBorderFocus]          = WEB(0x8F84FF), \
  [brBorderActive]         = WEB(0x3A3D46), \
  [brFolderText]           = WEB(0xA79FFF), \
  [brColumnViewBg]         = WEB(0x141519), \
  [brModalOverlay]         = 0x60000000, \
  [brToolbarForeground]    = WEB(0xC9CBD2), \
  [brPanelDark]            = WEB(0x1A1B20), \
  [brPanelDarker]          = WEB(0x18191D), \
  [brTabBarBg]             = WEB(0x15161A), \
  [brTextWarning]          = WEB(0xF2B24C), \
  [brTextInfo]             = WEB(0x6FA8FF), \
  [brTextSecondary]        = WEB(0x9A9EA8), \
  [brTextOnColor]          = WEB(0x101114), \
  [brSelectionTop]         = WEB(0x383274), \
  [brSelectionBottom]      = WEB(0x383274), \
  [brPlasticNeutral]       = WEB(0x5A5D66), \
  [brAccentText]           = WEB(0xFFFFFF), \
  [brDesktopBg]            = WEB(0x2B2E36)

static const uint32_t k_theme_palette_midnight[brCount]
  __attribute__((unused)) = { THEME_PALETTE_MIDNIGHT_INIT };

#endif
