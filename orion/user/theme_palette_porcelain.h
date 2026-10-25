#ifndef __THEME_PALETTE_PORCELAIN_H__
#define __THEME_PALETTE_PORCELAIN_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Porcelain: cool white surfaces and an indigo accent, after macOS Finder
// and modern banking dashboards. Selection is a solid indigo row with white
// text; fields are white wells with a hairline that turns indigo on focus.
// Its dark counterpart is theme_palette_porcelain_dark.h.

#define THEME_PALETTE_PORCELAIN_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0xF8F9FB), \
  [brWindowDarkBg]         = WEB(0xF0F2F5), \
  [brWorkspaceBg]          = WEB(0xE6E9EE), \
  [brActiveTitlebar]       = WEB(0xF0F2F5), \
  [brActiveTitlebarText]   = WEB(0x13151A), \
  [brInactiveTitlebar]     = WEB(0xF6F7F9), \
  [brInactiveTitlebarText] = WEB(0x9AA0AA), \
  [brStatusbarBg]          = WEB(0xF3F4F7), \
  [brLightEdge]            = WEB(0xD8DCE3), \
  [brDarkEdge]             = WEB(0xE3E6EB), \
  [brFlare]                = WEB(0xFFFFFF), \
  [brAccent]               = WEB(0x4C5BD4), \
  [brButtonInner]          = WEB(0xF1F3F6), \
  [brButtonHover]          = WEB(0xE6E9EE), \
  [brTextNormal]           = WEB(0x13151A), \
  [brTextDisabled]         = WEB(0xA9AEB8), \
  [brTextError]            = WEB(0xD23B30), \
  [brTextSuccess]          = WEB(0x17855A), \
  [brBorderFocus]          = WEB(0x4C5BD4), \
  [brBorderActive]         = WEB(0xCBD0D8), \
  [brFolderText]           = WEB(0x3D4BB8), \
  [brColumnViewBg]         = WEB(0xFFFFFF), \
  [brModalOverlay]         = 0x40000000, \
  [brToolbarForeground]    = WEB(0x3B404A), \
  [brPanelDark]            = WEB(0xEEF0F4), \
  [brPanelDarker]          = WEB(0xE4E7EC), \
  [brTabBarBg]             = WEB(0xE9ECF0), \
  [brTextWarning]          = WEB(0xB26B00), \
  [brTextInfo]             = WEB(0x2F6FD6), \
  [brTextSecondary]        = WEB(0x646A75), \
  [brTextOnColor]          = WEB(0x13151A), \
  [brSelectionTop]         = WEB(0x5866DC), \
  [brSelectionBottom]      = WEB(0x4C5BD4), \
  [brPlasticNeutral]       = WEB(0xC8CDD5), \
  [brAccentText]           = WEB(0xFFFFFF), \
  [brDesktopBg]            = WEB(0xC9CFDA)

static const uint32_t k_theme_palette_porcelain[brCount]
  __attribute__((unused)) = { THEME_PALETTE_PORCELAIN_INIT };

#endif
