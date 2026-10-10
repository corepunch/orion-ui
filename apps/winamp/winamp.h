// Winamp — a classic Winamp 2 player for iPhone. Landscape shows the main
// window alone, scaled up; portrait stacks the player, equalizer and playlist.
//
// The windows are drawn from a Winamp 2.x skin (an unpacked folder or a .wsz
// in Documents). A skin is a fixed bitmap atlas with fixed coordinates, so
// each window composes its sprites into a skin-resolution canvas and draws it
// scaled to the screen; skin hit regions are tables.

#ifndef __WINAMP_H__
#define __WINAMP_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/user/accel.h>
#include <orion/user/rect.h>
#include <orion/kernel/renderer.h>
#include "build/generated/apps/winamp/winamp.h"

#ifndef WINAMP_DEBUG
#define WINAMP_DEBUG 0
#endif
#define WA_DEBUG(...) do { if (WINAMP_DEBUG) { fprintf(stderr, "[wa] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

#define WA_W          275
#define WA_MAIN_H     116
#define WA_EQ_H       116
#define WA_PL_MIN_H   116
#define WA_RATE       44100
#define WA_BANDS      10
#define WA_VIS_BARS   19
#define WA_VIS_N      512
#define WA_TICK_MS    33
#define WA_DROP_BATCH_TICKS 15    // drops this close together count as one drag

enum {
  SKIN_MAIN, SKIN_TITLEBAR, SKIN_CBUTTONS, SKIN_SHUFREP, SKIN_POSBAR, SKIN_VOLUME, SKIN_BALANCE,
  SKIN_PLAYPAUS, SKIN_MONOSTER, SKIN_NUMBERS, SKIN_TEXT, SKIN_EQMAIN, SKIN_PLEDIT, SKIN_COUNT
};

typedef struct {
  bitmap_t *bmp[SKIN_COUNT];
  uint32_t vis[24];                                       // packed RGBA (0xAABBGGRR)
  uint32_t pl_normal, pl_current, pl_normal_bg, pl_selected_bg;
} wa_skin_t;

typedef struct { int w, h; } wa_canvas_t;                  // skin-pixel extent; drawn at g_app->pt_per_px
typedef struct { uint16_t id; irect16_t r; } wa_region_t;   // skin hit region

typedef enum { WA_STOPPED, WA_PLAYING, WA_PAUSED } wa_state_t;

typedef struct { float b0, b1, b2, a1, a2; } wa_biquad_t;

typedef struct {
  // Owned by the audio thread while the device runs; the UI changes it under axAudioLock.
  uint8_t *file;
  size_t file_size;
  void *dec;                       // mp3dec_ex_t
  int hz, channels;
  uint64_t frames, pos;            // source frames
  int16_t buf[4096];
  int buf_len, buf_at;
  float cur[2], nxt[2], phase;
  bool ended;
  wa_state_t state;
  float volume, balance;           // 0..1, -1..1
  bool eq_on;
  wa_biquad_t eq[WA_BANDS];
  float eq_z[WA_BANDS][2][2];
  float preamp_gain;
  float vis_ring[WA_VIS_N];        // mono, written by the callback, read by the UI tick
  int vis_at;
} wa_engine_t;

typedef struct {
  char *path;
  char *title;                     // "Artist - Title" or the file name
  int seconds;                     // -1 until known
} wa_track_t;

typedef struct {
  wa_track_t *items;
  int count, cap;
  int current, selected, scroll;   // scroll in rows
} wa_playlist_t;

typedef struct {
  hinstance_t hinstance;
  window_t *win, *player, *equalizer, *playlist;
  accel_table_t *accel;
  uint32_t timer;
  int audio_dev;
  wa_engine_t engine;
  wa_skin_t skin;
  wa_playlist_t list;
  // UI state
  bool show_eq, show_pl, shuffle, repeat, eq_auto, time_remaining, landscape;
  float eq_db[WA_BANDS], preamp_db;   // -12..+12
  int eq_preset;
  int kbps, khz, channels;
  uint8_t vis_bars[WA_VIS_BARS], vis_peaks[WA_VIS_BARS];
  float vis_peak_hold[WA_VIS_BARS];
  int marquee_px, tick;
  int drop_tick;                   // tick of the last drop that started playback
  float pt_per_px;                 // logical points per skin pixel
} winamp_t;

extern winamp_t *g_app;

// controller.c
winamp_t *app_init(void);
void app_shutdown(winamp_t *app);
void app_add_path(const char *path);
bool app_drop_file(const char *path, int index, bool play);   // index -1 appends
void app_set_skin(const char *path);
void app_command(uint16_t id);
void app_play_index(int index);
void app_tick(void);
void app_set_volume(float v);
void app_set_balance(float b);
void app_seek(float fraction);
void app_set_eq(int band, float db);       // band -1 = preamp
float app_position(void);                  // 0..1
int app_elapsed_seconds(void);
int app_track_seconds(void);
const char *app_marquee_text(char *buf, size_t n);
void app_relayout(void);
void app_invalidate_all(void);

// audio.c
bool engine_open(wa_engine_t *e, const char *path);
void engine_close(wa_engine_t *e);
void engine_seek(wa_engine_t *e, uint64_t frame);
void engine_render(wa_engine_t *e, int16_t *out, int frames);
void engine_set_eq(wa_engine_t *e, const float db[WA_BANDS], float preamp_db, bool on);

// analyzer.c
void analyzer_update(const float ring[WA_VIS_N], int at, uint8_t bars[WA_VIS_BARS], uint8_t peaks[WA_VIS_BARS],
                     float hold[WA_VIS_BARS], bool active);

// playlist.c
void playlist_clear(wa_playlist_t *pl);
bool playlist_add(wa_playlist_t *pl, const char *path);
void playlist_remove(wa_playlist_t *pl, int index);
void playlist_move(wa_playlist_t *pl, int from, int to);
int  playlist_scan(wa_playlist_t *pl, const char *dir);
bool mp3_probe(const char *path, char **title, int *seconds);

// skin.c
bool skin_load(wa_skin_t *s, const char *path);
bool skin_load_default(wa_skin_t *s);
void skin_free(wa_skin_t *s);
bool canvas_resize(wa_canvas_t *c, int w, int h);
void canvas_free(wa_canvas_t *c);
void canvas_fill(wa_canvas_t *c, irect16_t r, uint32_t rgba);
void canvas_blit(wa_canvas_t *c, int sheet, irect16_t src, int dx, int dy);
void canvas_tile(wa_canvas_t *c, int sheet, irect16_t src, irect16_t dst);
void canvas_text(wa_canvas_t *c, const char *text, int x, int y, int max_w, int scroll_px);
void canvas_digit(wa_canvas_t *c, int digit, int x, int y);
ipoint16_t skin_point(window_t *win, const wa_canvas_t *c, uint32_t wparam);
irect16_t skin_rect(irect16_t r);                           // skin pixels -> window points
window_t *skin_add_control(window_t *parent, const char *class_name, uint16_t id);
void skin_place(window_t *child, irect16_t skin_px);        // frame = skin_rect(skin_px)
int  skin_slider_pos(window_t *parent, uint16_t id);        // value of the SpriteSlider child `id`
bool skin_slider_dragging(window_t *parent, uint16_t id);
// Sprite sets for the skin's controls; each is the skin's own artwork, stretched to the control.
void skin_button_sprites(int sheet, irect16_t up, irect16_t down, sprite_button_t *out);
void skin_toggle_sprites(int sheet, irect16_t up, irect16_t down, irect16_t on, irect16_t on_down, sprite_button_t *out);

// Views
result_t win_winamp_main(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_winamp_player(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_winamp_equalizer(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_winamp_playlist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void skin_view_measure(int skin_h, layout_measure_t *m);
void player_apply_skin(window_t *win);                     // re-send sprites after a skin change
void eq_apply_skin(window_t *win);

#endif
