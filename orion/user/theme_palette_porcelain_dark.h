#ifndef __THEME_PALETTE_PORCELAIN_DARK_H__
#define __THEME_PALETTE_PORCELAIN_DARK_H__

#include <stdint.h>
#include <orion/user/theme.h>

// Porcelain Dark: Porcelain's dark counterpart, in the colours of Atom's One
// Dark. Charcoal faces with the editor-grey wells a step lighter, soft
// blue-grey ink instead of white, and muted status hues, so long tables read
// without glare. Edges are darker than the faces, as One Dark's borders are;
// the assistant's balloon is a raised One Dark popover with a soft outline.

#define THEME_PALETTE_PORCELAIN_DARK_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = WEB(0x21252B), \
  [brWindowDarkBg]         = WEB(0x1E2227), \
  [brWorkspaceBg]          = WEB(0x1B1D23), \
  [brActiveTitlebar]       = WEB(0x21252B), \
  [brActiveTitlebarText]   = WEB(0xD7DAE0), \
  [brInactiveTitlebar]     = WEB(0x1E2227), \
  [brInactiveTitlebarText] = WEB(0x5C6370), \
  [brStatusbarBg]          = WEB(0x1E2227), \
  [brLightEdge]            = WEB(0x3A3F4B), \
  [brDarkEdge]             = WEB(0x181A1F), \
  [brFlare]                = WEB(0x3E4451), \
  [brAccent]               = WEB(0x568AF2), \
  [brButtonInner]          = WEB(0x2C313A), \
  [brButtonHover]          = WEB(0x353B45), \
  [brTextNormal]           = WEB(0xABB2BF), \
  [brTextDisabled]         = WEB(0x4B5263), \
  [brTextError]            = WEB(0xE06C75), \
  [brTextSuccess]          = WEB(0x98C379), \
  [brBorderFocus]          = WEB(0x568AF2), \
  [brBorderActive]         = WEB(0x3A3F4B), \
  [brFolderText]           = WEB(0x61AFEF), \
  [brColumnViewBg]         = WEB(0x282C34), \
  [brModalOverlay]         = 0x60000000, \
  [brToolbarForeground]    = WEB(0x9DA5B4), \
  [brPanelDark]            = WEB(0x1E2227), \
  [brPanelDarker]          = WEB(0x1B1D23), \
  [brTabBarBg]             = WEB(0x1D2025), \
  [brTextWarning]          = WEB(0xE5C07B), \
  [brTextInfo]             = WEB(0x61AFEF), \
  [brTextSecondary]        = WEB(0x7F848E), \
  [brTextOnColor]          = WEB(0x1B1D23), \
  [brSelectionTop]         = WEB(0x4D78CC), \
  [brSelectionBottom]      = WEB(0x4570C2), \
  [brPlasticNeutral]       = WEB(0x4B5263), \
  [brAccentText]           = WEB(0xF0F2F5), \
  [brTooltipBackground]   = WEB(0x1B1D23), \
  [brTooltipText]         = WEB(0xFFFFFF), \
  [brInfoBackground]      = WEB(0x2C313A), \
  [brInfoText]            = WEB(0xC8CCD4), \
  [brDesktopBg]            = WEB(0x16181D)

static const uint32_t k_theme_palette_porcelain_dark[brCount]
  __attribute__((unused)) = { THEME_PALETTE_PORCELAIN_DARK_INIT };

#endif
