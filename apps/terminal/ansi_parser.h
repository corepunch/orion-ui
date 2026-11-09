// ANSI escape sequence parser for VGA terminal emulator.
// Consumes raw bytes from PTY and updates the screen state.

#ifndef __VGAT_ANSI_PARSER_H__
#define __VGAT_ANSI_PARSER_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct vgat_screen_s vgat_screen;

typedef struct {
  vgat_screen *screen;
  void (*write_cell)(vgat_screen *s, uint32_t codepoint, int fg, int bg);
  void (*newline)(vgat_screen *s);
  void (*backspace)(vgat_screen *s);
  void (*cr)(vgat_screen *s);
  void (*cursor_left)(vgat_screen *s);
  void (*cursor_right)(vgat_screen *s);
  void (*cursor_up)(vgat_screen *s);
  void (*cursor_down)(vgat_screen *s);
  void (*set_fg)(vgat_screen *s, int fg);
  void (*set_bg)(vgat_screen *s, int bg);
  void (*reset)(vgat_screen *s);
  void (*save_cursor)(vgat_screen *s);
  void (*restore_cursor)(vgat_screen *s);
  void (*erase_display)(vgat_screen *s, int mode);
  void (*erase_line)(vgat_screen *s, int mode);
  void (*cursor_pos)(vgat_screen *s, int row, int col);
  void (*set_title)(void *ud, const char *title);
  void (*set_palette_color)(void *ud, int idx, uint32_t rgba);
  void (*reset_palette)(void *ud);
} vgat_parser_callbacks_t;

typedef struct vgat_parser_s {
  int state;
  int params[16];
  int nparams;
  char private_marker;
  int cur_fg;
  int cur_bg;
  bool bold;
  uint32_t utf8_codepoint;
  uint8_t utf8_remaining;
  vgat_screen *screen;
  void (*write_cell)(vgat_screen *s, uint32_t codepoint, int fg, int bg);
  void (*newline)(vgat_screen *s);
  void (*backspace)(vgat_screen *s);
  void (*cr)(vgat_screen *s);
  void (*cursor_left)(vgat_screen *s);
  void (*cursor_right)(vgat_screen *s);
  void (*cursor_up)(vgat_screen *s);
  void (*cursor_down)(vgat_screen *s);
  void (*set_fg)(vgat_screen *s, int fg);
  void (*set_bg)(vgat_screen *s, int bg);
  void (*reset)(vgat_screen *s);
  void (*save_cursor)(vgat_screen *s);
  void (*restore_cursor)(vgat_screen *s);
  void (*erase_display)(vgat_screen *s, int mode);
  void (*erase_line)(vgat_screen *s, int mode);
  void (*cursor_pos)(vgat_screen *s, int row, int col);
  void (*set_title)(void *ud, const char *title);
  void (*set_palette_color)(void *ud, int idx, uint32_t rgba);
  void (*reset_palette)(void *ud);

  // OSC accumulator
  char osc_buf[256];
  int  osc_len;
  bool osc_st_pending; // true after ESC in OSC, waiting for ST
  void *userdata;      // opaque pointer for callbacks (e.g. window_t*)
} vgat_parser_t;

void vgat_parser_init(vgat_parser_t *p, const vgat_parser_callbacks_t *cbs);

void vgat_parser_feed(vgat_parser_t *p, const uint8_t *data, int len);

#endif // __VGAT_ANSI_PARSER_H__
