#ifndef __UI_KERNEL_H__
#define __UI_KERNEL_H__

#include <stdbool.h>
#include "../platform/platform.h"
#include "renderer.h"

#define UI_INIT_DESKTOP 0x01000000u
#define UI_INIT_TRAY 0x02000000u

#ifndef UI_WINDOW_SCALE
#define UI_WINDOW_SCALE 2
#endif

// Event type abstraction — maps to the platform AXmessage struct
typedef struct AXmessage ui_event_t;

// Event message queue functions
int get_message(ui_event_t *evt);
void dispatch_message(ui_event_t *evt);
void repost_messages(void);

// Graphics context initialization (abstracted)
bool ui_init_graphics(int flags, const char *title, int width, int height);
void ui_shutdown_graphics(void);

// Joystick input management (abstracted)
bool ui_joystick_init(void);
void ui_joystick_shutdown(void);
bool ui_joystick_available(void);
const char* ui_joystick_get_name(void);

// Per-frame rendering hooks: call ui_begin_frame() before drawing and
// ui_end_frame() after (replaces the old SDL_GL_SwapWindow).
void ui_begin_frame(void);
void ui_end_frame(void);

// Timing functions
void ui_delay(unsigned int milliseconds);

// Modifier state accessor — returns current AX_MOD_* flags
uint32_t ui_get_mod_state(void);

// Sprite stuff
int get_sprite_prog(void);
int get_sprite_vao(void);

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha);
void set_projection(int x, int y, int w, int h);
float *get_sprite_matrix(void);

// Application lifecycle — prefer these over direct access to 'running'.
// ui_is_running()   returns true while the event loop should keep going.
// ui_request_quit() signals the event loop to stop (analogous to PostQuitMessage).
// In BUILD_AS_GEM mode these are provided as macros by gem_magic.h instead.
#ifndef BUILD_AS_GEM
bool ui_is_running(void);
void ui_request_quit(void);
#endif

extern bool mode;
extern unsigned frame;

typedef enum {
	kSystemMetricScreenWidth,
	kSystemMetricScreenHeight,
} ui_system_metrics_t;

int ui_get_system_metrics(ui_system_metrics_t);
void ui_update_screen_size(int width, int height);

#endif
