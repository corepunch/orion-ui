#ifndef __UI_KERNEL_H__
#define __UI_KERNEL_H__

#include <stdbool.h>
#include "../platform/platform.h"
#include "renderer.h"

typedef struct bitmap_strip_s bitmap_strip_t;

#define UI_INIT_DESKTOP 0x01000000u
#define UI_INIT_TRAY 0x02000000u

#ifndef UI_WINDOW_SCALE
#define UI_WINDOW_SCALE 1
#endif

// Logical font-pixel height used to derive chrome dimensions (titlebar,
// menubar, list row heights, etc.).  This is a compile-time constant tied to
// the scale factor rather than the runtime-loaded font's cell height.
//
// Measured glyph heights:
//   UI_WINDOW_SCALE == 1  → ChiKareGo2 (16x16 cells, max glyph h = 12 px)
//   UI_WINDOW_SCALE >= 2  → SmallFont  ( 8x 8 cells, cell  h = 8  px)
//
// FONT_SIZE       — system (chrome) font: ChiKareGo2/Chicago12 at scale=1
// FONT_SIZE_SMALL — small (content) font: Geneva9/SmallFont at all scales
// FONT_PIXEL_SIZE — cell height of the system font (for vertical centering)
#if UI_WINDOW_SCALE == 1
#  define FONT_SIZE        12
#  define FONT_PIXEL_SIZE  16
#  define FONT_SIZE_SMALL   12   // Geneva12 / SmallFont cell height at scale=1
#else
#  define FONT_SIZE        8
#  define FONT_PIXEL_SIZE  8
#  define FONT_SIZE_SMALL  8   // same as FONT_SIZE at scale>=2 (single font)
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

// Returns the directory that contains the running executable (no trailing
// slash, static buffer).  Returns "" on error.  Useful for resolving
// paths to data files (e.g. "<exe_dir>/../share/<appname>/icon.png").
const char *ui_get_exe_dir(void);

// Modifier state accessor — returns current AX_MOD_* flags
uint32_t ui_get_mod_state(void);

// Sprite stuff
int get_sprite_prog(void);
int get_sprite_vao(void);

typedef enum {
	UI_RENDER_EFFECT_COPY = 0,
	UI_RENDER_EFFECT_MASK_GRAYSCALE = 1,
	UI_RENDER_EFFECT_LEVELS = 2,
	UI_RENDER_EFFECT_INVERT = 3,
	UI_RENDER_EFFECT_THRESHOLD = 4,
	UI_RENDER_EFFECT_COUNT
} ui_render_effect_t;

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
void push_sprite_effect_args(int tex, int x, int y, int w, int h, float alpha,
                             ui_render_effect_t effect,
                             const ui_render_effect_params_t *params);
void draw_rect_blend(int tex, int x, int y, int w, int h, float alpha,
                     ui_layer_blend_t blend);
void draw_rect_effect(int tex, int x, int y, int w, int h,
                      ui_render_effect_t effect,
                      const ui_render_effect_params_t *params);
bool bake_texture_effect(int src_tex, int w, int h,
                         ui_render_effect_t effect,
                         const ui_render_effect_params_t *params,
                         uint32_t *out_tex);
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

// Built-in system icon strip.
// Returns a pointer to the global bitmap_strip_t for icon_sheet_16x16.png,
// or NULL if the sheet was not found at startup.  Icon values from icons.h
// (sysicon_*) start at SYSICON_BASE (0x10000); subtract SYSICON_BASE to get
// the strip index when calling btnSetImage.
//
// Most callers do not need this: toolbar buttons whose icon field is
// >= SYSICON_BASE are drawn from the sheet automatically.
//
// Example:
//   bitmap_strip_t *s = ui_get_sysicon_strip();
//   if (s)
//     send_message(btn, btnSetImage, sysicon_add - SYSICON_BASE, s);
bitmap_strip_t *ui_get_sysicon_strip(void);

// Transparency checkerboard texture used by apps that need a visible alpha
// background.  This is a 2x2 RGBA texture intended for repeated tiling.
extern uint32_t ui_transparency_checker_texture;

// Theme icon strip (theme.png, 128x16 grayscale, 8x8 tiles).
// Icons are indexed by theme_icon_t (user/theme.h).
// Used by draw_theme_icon() for all chrome icons (close, scrollbar arrows,
// checkmark, combobox arrow, resize grip).
// Returns NULL if the sheet was not found at startup.
bitmap_strip_t *ui_get_theme_strip(void);

// File-picker icon strip (filepicker.png, 16x16 RGBA tiles).
// Icons are indexed by icon_id_t (user/sysicons.h).
// Used exclusively by win_filelist via RVM_SETICONSTRIP.
// Returns NULL if the sheet was not found at startup.
bitmap_strip_t *ui_get_icons_strip(void);

#endif
