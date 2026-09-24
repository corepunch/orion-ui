#ifndef __UI_H__
#define __UI_H__

// Main UI framework header - includes all UI subsystems

// User subsystem (window management)
#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/text.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include <orion/user/accel.h>
#include <orion/user/image.h>
#include <orion/user/color.h>
#include <orion/user/database.h>

// Kernel subsystem (event management)
#include <orion/kernel/kernel.h>

// Common controls subsystem
#include <orion/commctl/commctl.h>

// Common dialogs subsystem
#include <orion/commdlg/commdlg.h>

// Shared dialog button IDs.
enum {
    ID_OK = 1,
    ID_CANCEL = 2,
    ID_CONTROL_BASE = 1000,
    ID_COMMAND_BASE = 2000,
};

#endif
