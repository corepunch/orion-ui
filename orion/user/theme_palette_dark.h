#ifndef __THEME_PALETTE_DARK_H__
#define __THEME_PALETTE_DARK_H__

#include <stdint.h>
#include <orion/user/messages.h>

// Pre-navy default / Classic colors. Process default before the navy retune:
// Modern drawing with this table (get_theme() did not apply the light palette).

#define THEME_PALETTE_DARK_INIT \
  [brTransparent]          = 0x00000000, \
  [brControlBg]            = 0xff3c3c3c, \
  [brWindowDarkBg]         = 0xff2c2c2c, \
  [brWorkspaceBg]          = 0xff1e1e1e, \
  [brActiveTitlebar]       = 0xffD77800, \
  [brActiveTitlebarText]   = 0xffffffff, \
  [brInactiveTitlebar]     = 0xff2c2c2c, \
  [brInactiveTitlebarText] = 0xff787878, \
  [brStatusbarBg]          = 0xff383838, \
  [brLightEdge]            = 0xff7f7f7f, \
  [brDarkEdge]             = 0xff1a1a1a, \
  [brFlare]                = 0xffcfcfcf, \
  [brAccent]               = 0xffD77800, \
  [brButtonInner]          = 0xff505050, \
  [brButtonHover]          = 0xff5a5a5a, \
  [brTextNormal]           = 0xffc0c0c0, \
  [brTextDisabled]         = 0xff808080, \
  [brTextError]            = 0xffff4444, \
  [brTextSuccess]          = 0xff44ff44, \
  [brBorderFocus]          = 0xff101010, \
  [brBorderActive]         = 0xff808080, \
  [brFolderText]           = 0xffa0d000, \
  [brColumnViewBg]         = 0xff544e47, \
  [brModalOverlay]         = 0x40402000, \
  [brToolbarForeground]    = 0xffd8d8d8

static const uint32_t k_theme_palette_dark[brCount]
  __attribute__((unused)) = { THEME_PALETTE_DARK_INIT };

#endif
