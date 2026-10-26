#ifndef __THEME_PALETTE_STUDIO_H__
#define __THEME_PALETTE_STUDIO_H__

#include <stdint.h>
#include <orion/user/messages.h>

// Modern creative workspace: charcoal header, raised tool surfaces and blue selection.

#define THEME_PALETTE_STUDIO_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = 0xff303230, \
  [brWindowDarkBg]         = 0xff242220, \
  [brWorkspaceBg]          = 0xff201d1b, \
  [brActiveTitlebar]       = 0xff242220, \
  [brActiveTitlebarText]   = 0xffffffff, \
  [brInactiveTitlebar]     = 0xff242220, \
  [brInactiveTitlebarText] = 0xff787878, \
  [brStatusbarBg]          = 0xff303230, \
  [brLightEdge]            = 0xff555653, \
  [brDarkEdge]             = 0xff20211f, \
  [brFlare]                = 0xffcfcfcf, \
  [brAccent]               = 0xffc57330, \
  [brButtonInner]          = 0xff454744, \
  [brButtonHover]          = 0xff484a47, \
  [brTextNormal]           = 0xfff2f1f0, \
  [brTextDisabled]         = 0xff80837d, \
  [brTextError]            = 0xff4444ff, \
  [brTextSuccess]          = 0xff44ff44, \
  [brBorderFocus]          = 0xff101010, \
  [brBorderActive]         = 0xff808080, \
  [brFolderText]           = 0xffa0d000, \
  [brColumnViewBg]         = 0xff242220, \
  [brModalOverlay]         = 0x40402000, \
  [brToolbarForeground]    = 0xfff2f1f0, \
  [brPanelDark]            = 0xff303230, \
  [brPanelDarker]          = 0xff303230, \
  [brTabBarBg]             = 0xff242220, \
  [brTextWarning]          = 0xff2ea8e5, \
  [brTextInfo]             = 0xfff0a04c, \
  [brTextSecondary]        = 0xffb1b4ad, \
  [brTextOnColor]          = 0xff101010, \
  [brSelectionTop]         = 0xffc57330, \
  [brSelectionBottom]      = 0xffc57330, \
  [brPlasticNeutral]       = 0xffd0d0d0, \
  [brAccentText]           = 0xffffffff, \
  [brDesktopBg]            = 0xff6B3529

static const uint32_t k_theme_palette_studio[brCount]
  __attribute__((unused)) = { THEME_PALETTE_STUDIO_INIT };

#endif
