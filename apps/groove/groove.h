#ifndef __GROOVE_H__
#define __GROOVE_H__

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/user/accel.h>
#include <orion/user/rect.h>
#include <orion/commctl/menubar.h>
#include "build/generated/apps/groove/groove.h"

#define SCREEN_W        1180
#define SCREEN_H        760

#define GR_SAMPLE_RATE  44100
#define GR_BEATS_BAR    4
#define GR_TRACKS       8
#define GR_SHEET_HEADER_W 32
#define GR_BARS         32
#define GR_MAX_BLOCKS   128
#define GR_MAX_CLIPS    256
#define GR_BPM_MIN      70
#define GR_BPM_MAX      170
#define GR_BPM_DEFAULT  120

// ── Block library ────────────────────────────────────────────────────────
// Drums..Electronic are the original loop families. Kicks..FX follow the
// dance-library roles in apps/groove/docs/dance-ejay-pxd.md.
typedef enum {
  CAT_DRUMS, CAT_BASS, CAT_PIANO, CAT_GUITAR, CAT_ELECTRONIC,
  CAT_KICK, CAT_SNARE, CAT_HAT, CAT_CLAP, CAT_CYMBAL,
  CAT_PERC, CAT_FILL, CAT_SCRATCH, CAT_ORGAN, CAT_VOX, CAT_FX,
  CAT_COUNT
} category_t;

#define GR_PEAKS_BAR 128
#define GR_PEAKS_MAX 512   // 4 bars

// Rendered audio plus its overview, swapped as one unit when the tempo changes.
typedef struct {
  float   *pcm;            // mono, GR_SAMPLE_RATE, exactly bars * bar_frames long
  int      frames;
  uint8_t  peaks[GR_PEAKS_MAX]; // waveform overview, GR_PEAKS_BAR columns per bar
  int      npeaks;
} block_pcm_t;

typedef struct {
  const char *name;
  category_t  cat;
  int         bars;       // 1, 2 or 4 — always snaps to whole bars
  block_pcm_t audio;
} block_t;

#define CAT_ALL CAT_COUNT // bin page listing every block

extern const char *const kCategoryName[CAT_COUNT];
uint32_t category_color(category_t cat);

int            blocks_count(void);
const block_t *block_get(int id);
int            blocks_in_category(category_t cat, int *ids, int max);
// Renders every block for `bpm` into out[]; blocks_swap() installs them and
// hands the previous buffers back so the caller can free them after unlocking.
void           blocks_render(int bpm, block_pcm_t out[GR_MAX_BLOCKS]);
void           blocks_swap(block_pcm_t io[GR_MAX_BLOCKS]);
void           blocks_free(void);
int            bar_frames_for_bpm(int bpm);

// ── Song + mixer ─────────────────────────────────────────────────────────
typedef struct { int block, track, bar; } clip_t;

typedef struct {
  clip_t   clips[GR_MAX_CLIPS];
  int      nclips;
  bool     mute[GR_TRACKS], solo[GR_TRACKS];
  int      bpm;
  int64_t  pos;           // playhead, in frames
  bool     playing, loop;
  int      preview_block; // -1 = none; one-shot audition from the bin
  int64_t  preview_pos;
} song_t;

void song_init(song_t *s);
int  song_length_bars(const song_t *s);
int  song_clip_at(const song_t *s, int track, int bar);          // clip index or -1
bool song_can_place(const song_t *s, int track, int bar, int bars, int ignore_clip);
int  song_add_clip(song_t *s, int block, int track, int bar);    // clip index or -1
void song_remove_clip(song_t *s, int idx);
// Mixes `frames` stereo float frames into lr[] (interleaved) and advances the transport.
void song_render(song_t *s, float *lr, int frames);

// ── App state ────────────────────────────────────────────────────────────
typedef struct {
  bool active;
  int  block;       // block being dragged
  int  from_clip;   // clip being moved, or -1 for a fresh block from the bin
  int  grab_bars;   // bars from the clip's left edge to the grab point
  int  track, bar;  // current snapped target; track -1 = outside the sheet
  bool valid;       // target is free
} drag_t;

typedef struct {
  window_t     *win, *menubar_win, *sheet, *tabs, *library;
  accel_table_t *accel;
  hinstance_t   hinstance;
  song_t        song;
  int           audio_dev;
  uint32_t      timer;
  int           selected_clip;
  drag_t        drag;
  char          search[64]; // library filter, matched against block and category names
} groove_t;

extern groove_t *g_app;

// Private messages.
enum {
  shDragOver = evUser + 5000, // wparam = MAKEDWORD(screen_x, screen_y)
  shDrop,                     // wparam = MAKEDWORD(screen_x, screen_y)
  shDragEnd,                  // clear drag preview
  shSeekBar,                  // wparam = bar
  binFilter,                  // re-apply g_app->search to a bin page
};

#define ID_PLAY      ID_TRANSPORT_PLAY
#define ID_STOP      ID_TRANSPORT_STOP
#define ID_REWIND    ID_TRANSPORT_REWIND
#define ID_LOOP      ID_TRANSPORT_LOOP
#define ID_BPM_UP    ID_TRANSPORT_FASTER
#define ID_BPM_DOWN  ID_TRANSPORT_SLOWER
#define ID_DELETE    ID_EDIT_DELETE
#define ID_TABS      200
#define ID_SEARCH    201

// Bin tiles and sheet clips. NOACTIVATE so creating one does not take focus.
#define GR_CARD_FLAGS (WINDOW_NOFILL | WINDOW_TRANSPARENT | WINDOW_NODRAG | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP)

// Controller (controller.c)
groove_t *app_init(void);
void      app_shutdown(groove_t *app);
void      app_load_demo(void);
void      app_new_song(void);
void      create_menubar(void);
void      app_lock(void);
void      app_unlock(void);
void      app_command(uint16_t id);
void      app_set_playing(bool playing);
void      app_seek_bar(int bar);
void      app_set_bpm(int bpm);
void      app_preview(int block);
void      app_select_clip(int idx);
bool      app_drop(const drag_t *d);          // commits a drag (add or move)
void      app_update_status(void);
void      app_set_search(const char *text);
bool      block_matches(int id, const char *query);

// Views
extern result_t main_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_sheet(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_bin(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_library(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void toolbar_refresh(window_t *win);
void draw_clip(window_t *win, const block_t *b, irect16_t r, uint32_t color, ctrl_state_t state);
// Waveform overview of `b` resampled to column x of a w-pixel-wide strip, 0..255.
float block_peak(const block_t *b, int x, int w);
// Moves a card window. A visual drag keeps the painted copy still: the frame
// delta is added back into the drag offset.
void card_place(window_t *card, irect16_t cell);

#endif
