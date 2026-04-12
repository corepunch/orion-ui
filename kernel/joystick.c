/**
 * Joystick Input Implementation (Platform Backend)
 */

#include <stdio.h>
#include <stdbool.h>

#include "../platform/platform.h"
#include "joystick.h"

static bool g_joystick_open = false;

bool ui_joystick_init(void) {
  if (axJoystickInit()) {
    g_joystick_open = true;
    printf("Opened joystick: %s\n", axJoystickGetName());
    return true;
  }
  printf("No joystick/gamepad devices found\n");
  return false;
}

void ui_joystick_shutdown(void) {
  if (g_joystick_open) {
    axJoystickShutdown();
    g_joystick_open = false;
  }
}

bool ui_joystick_available(void) {
  return g_joystick_open;
}

const char* ui_joystick_get_name(void) {
  if (g_joystick_open) {
    return axJoystickGetName();
  }
  return NULL;
}
