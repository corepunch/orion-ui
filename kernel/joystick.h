#ifndef __UI_JOYSTICK_H__
#define __UI_JOYSTICK_H__

/**
 * Joystick Input Abstraction Layer
 *
 * This module wraps the platform (corepunch/platform) joystick API,
 * routing joystick events through the Orion window message system.
 */

#include <stdbool.h>

// Opaque joystick handle

/**
 * Initialize joystick subsystem.
 * Opens the first available joystick/gamepad device via WI_JoystickInit().
 *
 * Returns true if a joystick was successfully opened, false otherwise.
 */
bool ui_joystick_init(void);

/**
 * Shutdown joystick subsystem.
 * Closes any open joystick device via WI_JoystickShutdown().
 */
void ui_joystick_shutdown(void);

/**
 * Check if a joystick is currently connected.
 *
 * Returns true if a joystick is available, false otherwise.
 */
bool ui_joystick_available(void);

/**
 * Get the name of the connected joystick.
 *
 * Returns the joystick name string, or NULL if no joystick is connected.
 */
const char* ui_joystick_get_name(void);

#endif
