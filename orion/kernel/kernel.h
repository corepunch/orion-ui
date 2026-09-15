#ifndef __UI_KERNEL_H__
#define __UI_KERNEL_H__

#include <stdbool.h>
#include <platform/platform.h>
#include "renderer.h"

typedef struct bitmap_strip_s bitmap_strip_t;

#define UI_INIT_DESKTOP 0x01000000u
#define UI_INIT_TRAY 0x02000000u
#define UI_INIT_HIDDEN  0x04000000u

#ifndef UI_WINDOW_SCALE
#define UI_WINDOW_SCALE 1
#endif

#ifndef ORION_ALLOW_HIGHDPI
#define ORION_ALLOW_HIGHDPI 1
#endif

// Logical font-pixel height used to derive chrome dimensions (titlebar,
// menubar, list row heights, etc.).  This is a compile-time constant tied to
// the scale factor rather than the runtime-loaded font's cell height.
//
// FONT_SIZE       — system chrome font height
// FONT_SIZE_SMALL — content font height
// FONT_PIXEL_SIZE — rendered system line height (for vertical centering)
#if UI_WINDOW_SCALE == 1
#  define FONT_SIZE        12
#  define FONT_PIXEL_SIZE  14
#  define FONT_SIZE_SMALL  12
#else
#  define FONT_SIZE        8
#  define FONT_PIXEL_SIZE  8
#  define FONT_SIZE_SMALL  8
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
// ui_end_frame() afterward to present through the active platform backend.
void ui_begin_frame(void);
void ui_end_frame(void);

// Timing functions
void ui_delay(unsigned int milliseconds);

// Returns the directory that contains the running executable (no trailing
// slash, static buffer).  Returns "" on error.  Useful for resolving
// paths to data files (e.g. "<exe_dir>/../share/<appname>/icon.png").
const char *ui_get_exe_dir(void);

// Modifier state accessor — returns current AX_MOD_* flags
uint32_t ui_get_mod_state(void);

// Keyboard state accessor — true while the AX_KEY_* key is held.
bool ui_is_key_down(uint32_t key);

// Sprite stuff
int get_sprite_prog(void);
int get_sprite_vao(void);

typedef struct {
	float f[8];
} ui_render_effect_params_t;

typedef enum {
	UI_LAYER_BLEND_NORMAL = 0,
	UI_LAYER_BLEND_MULTIPLY = 1,
	UI_LAYER_BLEND_SCREEN = 2,
	UI_LAYER_BLEND_ADD = 3,
} ui_layer_blend_t;

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha);
void draw_rect_blend(int tex, int x, int y, int w, int h, float alpha,
                     ui_layer_blend_t blend);
void draw_rect_program_blend(int tex, int x, int y, int w, int h, float alpha,
                             ui_layer_blend_t blend, uint32_t program,
                             float mix_amount);
void draw_rect_gradient(int tex, int x, int y, int w, int h,
                        const ui_render_effect_params_t *params);
void draw_rect_program_params_blend(int tex, int x, int y, int w, int h,
                                    float alpha, ui_layer_blend_t blend,
                                    uint32_t program, float mix_amount,
                                    const ui_render_effect_params_t *params);
void draw_rect_program_params(int tex, int x, int y, int w, int h,
                              uint32_t program, float mix_amount,
                              const ui_render_effect_params_t *params);
void draw_rect_program(int tex, int x, int y, int w, int h, uint32_t program,
                       float mix_amount);
bool bake_texture_program_params(int src_tex, int w, int h, uint32_t program,
                                 float mix_amount,
                                 const ui_render_effect_params_t *params,
                                 uint32_t *out_tex);
bool bake_texture_program(int src_tex, int w, int h, uint32_t program,
                          float mix_amount, uint32_t *out_tex);
bool read_texture_rgba(int src_tex, int w, int h, uint8_t *out_rgba);
bool capture_framebuffer_rgba(int w, int h, uint8_t *out_rgba);
bool ui_save_screenshot(const char *path, int quality);
// Queue a screenshot for the next fully painted frame.  The event loop reads
// the back buffer after all queued paints and presents it before optionally
// quitting, so callers never capture a partially drawn window hierarchy.
bool ui_request_screenshot(const char *path, int quality, bool quit_after);
// Internal event-loop boundary used to consume a request only on painted frames.
bool ui_dequeue_screenshot_for_frame(bool frame_had_paint, char *path,
                                     size_t path_size, int *quality,
                                     bool *quit_after);
bool ui_load_program_from_source(const char *vs_src, const char *fs_src,
                                 const char *attrib0, const char *attrib1,
                                 const char *attrib2, uint32_t *out_program);
void ui_delete_program(uint32_t program);
void set_projection(int x, int y, int w, int h);
float *get_sprite_matrix(void);
// Uniform 2D scale/rotation plus translation: x'=a*x-b*y+tx, y'=b*x+a*y+ty.
typedef struct { float a, b, tx, ty; } view_matrix_t;
void begin_draw_transform(const view_matrix_t *view, float saved[16]);
void end_draw_transform(const float saved[16]);

// Application lifecycle — prefer these over direct access to 'running'.
// ui_is_running()   returns true while the event loop should keep going.
// ui_request_quit() signals the event loop to stop (analogous to PostQuitMessage).
// In BUILD_AS_GEM mode these are provided as macros by gem.h instead.
#ifndef BUILD_AS_GEM
bool ui_is_running(void);
void ui_request_quit(void);
#endif

// Shell-execute hook — analogous to Win32 ShellExecute().
//
// ui_register_open_file_handler() is called once at startup by whoever owns
// the process (e.g. orion-shell) to register a handler for ui_open_file().
//
// ui_open_file() can be called by any code (file manager, other gems, …) to
// ask the current "shell" to open a file.  The handler receives the full path
// and returns true if it handled the file.  If no handler is registered (e.g.
// running standalone without a shell) the call is silently ignored.
//
// Typical shell registration:
//   ui_register_open_file_handler(shell_handle_open_file);
//
// Typical gem usage (filemanager opening a .gem or .lua):
//   if (!ui_open_file(item->path)) { /* fallback */ }
typedef bool (*ui_open_file_handler_t)(const char *path);
void ui_register_open_file_handler(ui_open_file_handler_t handler);
bool ui_open_file(const char *path);

extern bool mode;
extern unsigned frame;

/* Async HTTP/HTTPS client (kernel/http.h). */
#include "http.h"

typedef enum {
	kSystemMetricScreenWidth,
	kSystemMetricScreenHeight,
} ui_system_metrics_t;

int ui_get_system_metrics(ui_system_metrics_t);
void ui_update_screen_size(int width, int height);

// Transparency checkerboard texture used by apps that need a visible alpha
// background.  This is a 2x2 RGBA texture intended for repeated tiling.
extern uint32_t ui_transparency_checker_texture;

// Theme icon strip (theme.png, 128x16 grayscale, 8x8 tiles).
// Legacy bitmap accessor; window/control glyphs now resolve Lucide SVGs.
// Returns NULL if the sheet was not found at startup.
bitmap_strip_t *ui_get_theme_strip(void);

#endif
