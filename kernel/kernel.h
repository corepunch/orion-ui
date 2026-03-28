#ifndef __UI_KERNEL_H__
#define __UI_KERNEL_H__

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "renderer.h"

#define UI_INIT_DESKTOP 0x01000000u
#define UI_INIT_TRAY 0x02000000u

#ifndef UI_WINDOW_SCALE
#define UI_WINDOW_SCALE 2
#endif

// Event type abstraction
typedef SDL_Event ui_event_t;

// Custom SDL event type used to wake up SDL_WaitEvent() when an internal
// message is posted via post_message().  Initialized by ui_init_graphics().
// Value (Uint32)-1 means not yet registered (treated as "no wakeup events").
extern Uint32 g_ui_repaint_event;

// Event message queue functions
int get_message(ui_event_t *evt);
void dispatch_message(ui_event_t *evt);
void repost_messages(void);

// Graphics context initialization (abstracted)
bool ui_init_graphics(int flags, const char *title, int width, int height);
void ui_shutdown_graphics(void);

// Joystick input management (abstracted)
// See ui/kernel/joystick.h for full API
bool ui_joystick_init(void);
void ui_joystick_shutdown(void);
bool ui_joystick_available(void);
const char* ui_joystick_get_name(void);

// Timing functions
void ui_delay(unsigned int milliseconds);

// Sprite stuff
int get_sprite_prog(void);
int get_sprite_vao(void);

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha);
void set_projection(int x, int y, int w, int h);
float *get_sprite_matrix(void);

// Global SDL objects
extern SDL_Window* window;
extern SDL_GLContext ctx;
extern bool running;
extern bool mode;
extern unsigned frame;

typedef enum {
	kSystemMetricScreenWidth,
	kSystemMetricScreenHeight,
} ui_system_metrics_t;

int ui_get_system_metrics(ui_system_metrics_t);
void ui_update_screen_size(int width, int height);

#endif
