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
#define GR_TICKS_BAR    256
#define GR_SNAP_TICKS   (GR_TICKS_BAR / 4)
#define GR_TRACKS       8
#define GR_SHEET_HEADER_W 32
#define GR_BARS         32
#define GR_MAX_BLOCKS   512
#define GR_MAX_CLIPS    256
#define GR_BPM_MIN      70
#define GR_BPM_MAX      180
#define GR_BPM_DEFAULT  140

// ── Block library ────────────────────────────────────────────────────────
// A block has one instrument family and any number of genre tags; the
// library filters on both. Families follow the roles in apps/groove/docs/dance-ejay-pxd.md.
typedef enum {
  CAT_DRUMS, CAT_KICK, CAT_SNARE, CAT_HAT, CAT_CLAP, CAT_CYMBAL, CAT_PERC, CAT_FILL,
  CAT_BASS, CAT_KEYS, CAT_ORGAN, CAT_GUITAR, CAT_SYNTH, CAT_PAD, CAT_STAB,
  CAT_VOX, CAT_SCRATCH, CAT_FX,
  CAT_COUNT
} category_t;

// Genre tags are bit flags: a dry four-on-the-floor kick serves Dance, Rave
// and Techno alike, so it carries all three.
enum {
  GENRE_DANCE  = 1 << 0,
  GENRE_HIPHOP = 1 << 1,
  GENRE_RAVE   = 1 << 2,
  GENRE_TECHNO = 1 << 3,
  GENRE_ANY    = GENRE_DANCE | GENRE_HIPHOP | GENRE_RAVE | GENRE_TECHNO,
};
#define GENRE_COUNT 4

#define GR_PEAKS_BAR 128
#define GR_PEAKS_MAX 512   // 4 bars

// Rendered audio plus its overview, installed as one unit.
typedef struct {
  float   *pcm;            // mono, GR_SAMPLE_RATE, exactly bars * bar_frames long; NULL when only the overview is kept
  int      frames;
  uint8_t  peaks[GR_PEAKS_MAX]; // waveform overview, GR_PEAKS_BAR columns per bar
  int      npeaks;
} block_pcm_t;

typedef struct {
  const char *name;
  const char *display_name;
  uint16_t    variant;    // 0 = unversioned; otherwise a visible 1-based sample variant
  category_t  cat;
  uint8_t     genres;     // GENRE_* tags
  int         bars;       // duration: 1, 2 or 4 bars
  block_pcm_t audio;
  int         audio_bpm;  // tempo `audio` was rendered for; 0 = never rendered
  uint64_t    audio_revision;
} block_t;

extern const char *const kCategoryName[CAT_COUNT];
extern const char *const kGenreName[GENRE_COUNT]; // index = bit number of the GENRE_* flag
uint32_t category_color(category_t cat);

int            blocks_count(void);
const block_t *block_get(int id);
int            blocks_in_category(category_t cat, int *ids, int max);
// Blocks are synthesized on demand, one at a time (library.c, synth.c).
// block_render() is pure and leaves the result with the caller; the other
// three change what the audio callback reads, so call them under the audio lock.
bool           block_render(int id, int bpm, block_pcm_t *out);
bool           groove_mp3_load(const char *filename, int frames, float **pcm);
// Installs *io for `bpm` and hands the previous audio back in *io to free after unlocking.
void           block_install(int id, int bpm, block_pcm_t *io);
// Detaches the PCM and returns it for the caller to free; the overview stays.
float         *block_release(int id);
void           blocks_free(void);
int            bar_frames_for_bpm(int bpm);

// ── Song + mixer ─────────────────────────────────────────────────────────
typedef struct { int block, track, position; uint64_t order; } clip_t; // position in ticks

typedef struct {
  clip_t   clips[GR_MAX_CLIPS];
  int      nclips;
  uint64_t clip_order;    // latest drop wins when starts coincide
  bool     mute[GR_TRACKS], solo[GR_TRACKS];
  int      bpm;
  int64_t  pos;           // playhead, in frames
  bool     playing, loop;
  int      preview_block; // -1 = none; one-shot audition from the bin
  int64_t  preview_pos;
} song_t;

void song_init(song_t *s);
int  song_length_ticks(const song_t *s);
int  song_clip_end(const song_t *s, int idx); // effective endpoint; source duration stays intact
int  song_clip_at(const song_t *s, int track, int position);    // clip index or -1
bool song_can_place(const song_t *s, int track, int position, int ticks, int ignore_clip);
int  song_add_clip(song_t *s, int block, int track, int position); // clip index or -1
bool song_move_clip(song_t *s, int idx, int track, int position);
int64_t position_frames_for_bpm(int position, int bpm);
void song_remove_clip(song_t *s, int idx);
// Mixes `frames` stereo float frames into lr[] (interleaved) and advances the transport.
void song_render(song_t *s, float *lr, int frames);

// ── App state ────────────────────────────────────────────────────────────
typedef struct {
  bool active;
  int  block;       // block being dragged
  int  from_clip;   // clip being moved, or -1 for a fresh block from the bin
  ipoint16_t grab;  // exact cursor offset within the dragged card
  int  track, position; // snapped target in ticks; track -1 = outside the sheet
  bool valid;       // target is within song bounds
} drag_t;

typedef enum { GR_DROP_ANCHOR_SAMPLE, GR_DROP_ANCHOR_POINTER } groove_drop_anchor_t;

typedef struct {
  uint32_t texture;
  ipoint16_t size;
  int radius;
  uint64_t audio_revision;
} waveform_cache_t;

typedef struct {
  window_t     *win, *chrome, *menubar_win, *toolbar, *sheet, *library, *bin; // chrome: menu bar with the compact toolbar
  accel_table_t *accel;
  hinstance_t   hinstance;
  song_t        song;
  int           audio_dev;
  uint32_t      timer;
  int           selected_clip;
  char          filename[512];
  drag_t        drag;
  uint8_t       genre;      // library filter: 0 = every genre, else one GENRE_* flag
  int           category;   // library filter: the one category_t the bin shows
  int           auditioned; // block whose PCM the last audition loaded, or -1
  int           peak_credit;   // waveform overviews the cards may still render this timer tick
  bool          peaks_pending; // a card went without its overview; repaint next tick
  bool          shown_playing; // song.playing as the transport shows it; the mixer stops at the end
  uint32_t      pictograms;   // shared category atlas, in category_t order
  waveform_cache_t waveforms[GR_MAX_BLOCKS];
} groove_t;

extern groove_t *g_app;

// Private messages.
enum {
  shDragOver = evUser + 5000, // wparam = MAKEDWORD(screen_x, screen_y)
  shDrop,                     // wparam = MAKEDWORD(screen_x, screen_y)
  shDragEnd,                  // clear drag preview
  shSeekPosition,             // wparam = position in ticks
  shSetDropAnchor,             // wparam = groove_drop_anchor_t; default = sample origin
  binFilter,                  // re-apply the family and genre filters to the bin
  grCardSetBlock,              // wparam = block id
  grCardSetState,              // wparam = ctrl_state_t
};

#define ID_PLAY      ID_TRANSPORT_PLAY
#define ID_STOP      ID_TRANSPORT_STOP
#define ID_REWIND    ID_TRANSPORT_REWIND
#define ID_FORWARD   ID_TRANSPORT_FORWARD
#define ID_LOOP      ID_TRANSPORT_LOOP
#define ID_DELETE    ID_EDIT_DELETE
// Above every auto-assigned card id (1..GR_MAX_BLOCKS inside a bin), so
// get_window_item() cannot find a card first.
#define ID_FAMILY(cat) (ID_CONTROL_BASE + 4 + (cat)) // library toolbar: one button per category_t
#define ID_GENRE     (ID_CONTROL_BASE + 3)

#define GR_PEAKS_PER_TICK 4 // waveform overviews rendered per 33 ms tick while a bin fills in

// Bin tiles and sheet clips. NOACTIVATE so creating one does not take focus.
#define GR_CARD_FLAGS (WINDOW_NOFILL | WINDOW_TRANSPARENT | WINDOW_NODRAG | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP)

// Controller (controller.c)
groove_t *app_init(void);
void      app_shutdown(groove_t *app);
void      app_load_demo(void);
void      app_new_song(void);
bool      app_open_song(const char *path);
bool      app_save_song(const char *path);
void      create_menubar(void);
void      app_lock(void);
void      app_unlock(void);
void      app_command(uint16_t id);
void      app_set_playing(bool playing);
void      app_seek_position(int position);
void      app_set_bpm(int bpm);
void      app_preview(int block);
void      app_select_clip(int idx);
bool      app_drop(const drag_t *d);          // commits a drag (add or move)
void      app_set_genre(uint8_t genre);        // 0 = every genre, else one GENRE_* flag
void      app_set_category(int category);      // one category_t
bool      block_visible(int id);               // passes the family and genre filters
// Blocks load lazily. Audio stays loaded for blocks in the song and the one
// being auditioned; a card that only draws a waveform keeps the overview alone.
bool      app_block_audio(int id);
bool      app_block_peaks(int id);

// Views
extern result_t main_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_sheet(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_bin(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_block_card(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void transport_refresh(void);
ipoint16_t clip_cell_size(window_t *sheet, const block_t *b);
bool block_pictograms_load(groove_t *app);
bitmap_strip_t block_pictogram_strip(void); // the category atlas as an icon strip, in category_t order
// Shared logical-pixel alpha masks; audio revision and geometry determine reuse.
uint32_t waveform_texture(groove_t *app, int block, ipoint16_t size, int radius);
void waveform_cache_free(groove_t *app);
// Moves a card window. A visual drag keeps the painted copy still: the frame
// delta is added back into the drag offset.
void card_place(window_t *card, irect16_t cell);

#endif
