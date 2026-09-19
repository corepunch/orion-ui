#ifndef __THEME_PALETTE_LIGHT_H__
#define __THEME_PALETTE_LIGHT_H__

#include <stdint.h>
#include <orion/user/messages.h>

// Original Modern apply_palette: light WinUI surfaces, same accent as dark.

#define THEME_PALETTE_LIGHT_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = 0xffF3F3F3, \
  [brWindowDarkBg]         = 0xffE8E8E8, \
  [brWorkspaceBg]          = 0xffDCDCDC, \
  [brActiveTitlebar]       = 0xffD77800, \
  [brActiveTitlebarText]   = 0xffffffff, \
  [brInactiveTitlebar]     = 0xffF0F0F0, \
  [brInactiveTitlebarText] = 0xff767676, \
  [brStatusbarBg]          = 0xffF0F0F0, \
  [brLightEdge]            = 0xffffffff, \
  [brDarkEdge]             = 0xffC8C8C8, \
  [brFlare]                = 0xffffffff, \
  [brAccent]               = 0xffD77800, \
  [brButtonInner]          = 0xffE0E0E0, \
  [brButtonHover]          = 0xffD0D0D0, \
  [brTextNormal]           = 0xff1A1A1A, \
  [brTextDisabled]         = 0xff9E9E9E, \
  [brTextError]            = 0xffC42B1C, \
  [brTextSuccess]          = 0xff0F7B0F, \
  [brBorderFocus]          = 0xff005FB8, \
  [brBorderActive]         = 0xff868686, \
  [brFolderText]           = 0xff107C10, \
  [brColumnViewBg]         = 0xffDEE3EA, \
  [brModalOverlay]         = 0x40000000, \
  [brToolbarForeground]    = 0xff1A1A1A, \
  [brPanelDark]            = 0xffE6E6E6, \
  [brPanelDarker]          = 0xffD4D4D4

static const uint32_t k_theme_palette_light[brCount]
  __attribute__((unused)) = { THEME_PALETTE_LIGHT_INIT };

#endif
