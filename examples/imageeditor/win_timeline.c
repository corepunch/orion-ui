// Timeline palette window — horizontal strip showing animation frames.
// Compiled only when IMAGEEDITOR_ANIMATIONS == 1.

#include "imageeditor.h"

#if IMAGEEDITOR_ANIMATIONS

// ============================================================
// Layout constants
// ============================================================

// Control panel on the left: play/stop button + FPS label.
#define CTRL_BTN_W    40
#define CTRL_BTN_H    20
#define CTRL_FPS_H    14
#define CTRL_PAD       4

// Frame thumbnail cells.
#define CELL_PAD       3     // padding inside each cell
#define CELL_NAME_H   10     // height of the name label below the thumbnail
#define CELL_H        (TIMELINE_WIN_H - TITLEBAR_HEIGHT)
#define THUMB_H       (CELL_H - CELL_PAD*2 - CELL_NAME_H - 2)

// Colours
#define COL_TL_BG      MAKE_COLOR(0x2A, 0x2A, 0x2A, 0xFF)
#define COL_TL_BORDER  MAKE_COLOR(0x44, 0x44, 0x44, 0xFF)
#define COL_ACTIVE_BG  MAKE_COLOR(0x00, 0x78, 0xD7, 0xFF)
#define COL_HOVER_BG   MAKE_COLOR(0x3A, 0x3A, 0x3A, 0xFF)
#define COL_TEXT       MAKE_COLOR(0xDD, 0xDD, 0xDD, 0xFF)
#define COL_TEXT_DIM   MAKE_COLOR(0x88, 0x88, 0x88, 0xFF)

// ============================================================
// Window state
// ============================================================

typedef struct {
  int   scroll_x;    // horizontal scroll offset in pixels
  int   hover_cell;  // cell index under the mouse (-1 = none)
  bool  drag_active;
  int   drag_from;   // source cell index
  int   drag_x;      // current drag x position
  // Thumbnail textures — one per frame.
  GLuint  *thumbs;   // heap array, indexed by frame
  int      thumb_count;
  bool     thumbs_dirty; // true = rebuild all thumbnails
} timeline_state_t;

// ============================================================
// Helpers
// ============================================================

static canvas_doc_t *tl_doc(void) {
  return g_app ? g_app->active_doc : NULL;
}

// x offset of cell `n` in client space, accounting for the control panel and
// horizontal scroll.
static int cell_x(const timeline_state_t *st, int n) {
  return TIMELINE_CTRL_W + n * TIMELINE_THUMB_W - st->scroll_x;
}

// Hit-test: which frame cell is at client x?  Returns -1 if in control panel.
static int hit_cell(const timeline_state_t *st, int cx, int w) {
  if (cx < TIMELINE_CTRL_W) return -1;
  canvas_doc_t *doc = tl_doc();
  if (!doc || !doc->anim) return -1;
  for (int i = 0; i < doc->anim->frame_count; i++) {
    int x0 = cell_x(st, i);
    if (cx >= x0 && cx < x0 + TIMELINE_THUMB_W)
      return i;
  }
  (void)w;
  return -1;
}

// Ensure the thumb array matches the current frame count, releasing
// any textures beyond the new count and expanding the array as needed.
static void sync_thumb_array(timeline_state_t *st, int frame_count) {
  if (frame_count == st->thumb_count) return;

  if (frame_count < st->thumb_count) {
    for (int i = frame_count; i < st->thumb_count; i++) {
      if (st->thumbs[i]) {
        glDeleteTextures(1, &st->thumbs[i]);
        st->thumbs[i] = 0;
      }
    }
  }

  GLuint *t = realloc(st->thumbs, sizeof(GLuint) * (size_t)frame_count);
  if (!t && frame_count > 0) return; // OOM — keep old array, will re-try
  if (!t) { free(st->thumbs); st->thumbs = NULL; st->thumb_count = 0; return; }
  st->thumbs = t;
  for (int i = st->thumb_count; i < frame_count; i++)
    st->thumbs[i] = 0;
  st->thumb_count = frame_count;
}

static void rebuild_thumbnails(timeline_state_t *st) {
  canvas_doc_t *doc = tl_doc();
  if (!doc || !doc->anim) return;
  sync_thumb_array(st, doc->anim->frame_count);
  for (int i = 0; i < doc->anim->frame_count; i++) {
    if (i < st->thumb_count)
      anim_render_frame_thumbnail(doc->anim->frames[i],
                                  doc->canvas_w, doc->canvas_h,
                                  &st->thumbs[i]);
  }
  st->thumbs_dirty = false;
}

// Draw the left control panel (Play / Stop button and FPS display).
static void draw_ctrl_panel(const canvas_doc_t *doc, int h) {
  fill_rect(COL_TL_BG, R(0, 0, TIMELINE_CTRL_W, h));
  fill_rect(COL_TL_BORDER, R(TIMELINE_CTRL_W - 1, 0, 1, h));

  if (!doc || !doc->anim) return;
  bool playing = doc->anim->playing;

  // Play / Pause button.
  int bx = CTRL_PAD;
  int by = (h - CTRL_BTN_H) / 2 - CTRL_FPS_H / 2 - 2;
  uint32_t btn_bg = playing ? MAKE_COLOR(0xE0, 0x60, 0x00, 0xFF)
                             : MAKE_COLOR(0x00, 0x88, 0x00, 0xFF);
  fill_rect(btn_bg, R(bx, by, CTRL_BTN_W, CTRL_BTN_H));
  const char *label = playing ? "Stop" : "Play";
  int lx = bx + (CTRL_BTN_W - (int)strlen(label) * 6) / 2;
  draw_text_small(label, lx, by + (CTRL_BTN_H - 8) / 2, COL_TEXT);

  // FPS display.
  char fps_buf[16];
  snprintf(fps_buf, sizeof(fps_buf), "%d fps", doc->anim->fps);
  int fx = CTRL_PAD;
  int fy = by + CTRL_BTN_H + 4;
  draw_text_small(fps_buf, fx, fy, COL_TEXT_DIM);
}

// Draw a single frame cell at position cx in client space.
static void draw_cell(const timeline_state_t *st, int idx,
                      bool active, bool hover, int h) {
  int cx = cell_x(st, idx);
  int cw = TIMELINE_THUMB_W;

  uint32_t bg = active ? COL_ACTIVE_BG : (hover ? COL_HOVER_BG : COL_TL_BG);
  fill_rect(bg, R(cx, 0, cw, h));
  fill_rect(COL_TL_BORDER, R(cx + cw - 1, 0, 1, h));

  // Thumbnail.
  if (idx < st->thumb_count && st->thumbs[idx]) {
    int tx = cx + CELL_PAD;
    int ty = CELL_PAD;
    int tw = cw - CELL_PAD * 2;
    int th = THUMB_H;
    if (tw > 0 && th > 0)
      draw_rect(st->thumbs[idx], R(tx, ty, tw, th));
  }

  // Frame number label.
  char num_buf[8];
  snprintf(num_buf, sizeof(num_buf), "%d", idx + 1);
  int nx = cx + cw / 2 - (int)strlen(num_buf) * 3;
  int ny = h - CELL_NAME_H - 1;
  draw_text_small(num_buf, nx, ny, active ? MAKE_COLOR(0xFF,0xFF,0xFF,0xFF) : COL_TEXT_DIM);
}

// ============================================================
// Window proc
// ============================================================

static result_t timeline_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  timeline_state_t *st = (timeline_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      timeline_state_t *s = allocate_window_data(win, sizeof(timeline_state_t));
      s->hover_cell    = -1;
      s->drag_from     = -1;
      s->thumbs_dirty  = true;
      anim_render_init();
      return true;
    }

    case evDestroy: {
      if (st) {
        anim_render_shutdown();
        if (st->thumbs) {
          for (int i = 0; i < st->thumb_count; i++) {
            if (st->thumbs[i])
              glDeleteTextures(1, &st->thumbs[i]);
          }
          free(st->thumbs);
        }
      }
      if (g_app) g_app->timeline_win = NULL;
      return false;
    }

    case evSetFocus:
      if (g_app && tl_doc())
        g_app->active_doc = tl_doc();
      return false;

    case evPaint: {
      if (!st) return true;
      canvas_doc_t *doc = tl_doc();

      irect16_t cr = get_client_rect(win);
      int h = cr.h;
      int w = cr.w;
      fill_rect(COL_TL_BG, R(0, 0, w, h));

      if (st->thumbs_dirty)
        rebuild_thumbnails(st);

      draw_ctrl_panel(doc, h);

      if (doc && doc->anim) {
        int active = doc->anim->active_frame;
        for (int i = 0; i < doc->anim->frame_count; i++) {
          int cx = cell_x(st, i);
          if (cx + TIMELINE_THUMB_W < 0) continue;
          if (cx > w) break;
          draw_cell(st, i, i == active, i == st->hover_cell, h);
        }
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!st) return true;
      int mx = (int)(wparam & 0xFFFF);
      int my = (int)(wparam >> 16);
      irect16_t cr = get_client_rect(win);

      // Play/stop button hit.
      if (mx < TIMELINE_CTRL_W) {
        canvas_doc_t *doc = tl_doc();
        int h = cr.h;
        int by = (h - CTRL_BTN_H) / 2 - CTRL_FPS_H / 2 - 2;
        if (my >= by && my < by + CTRL_BTN_H)
          handle_menu_command(doc && doc->anim && doc->anim->playing
                              ? ID_ANIM_STOP : ID_ANIM_PLAY);
        return true;
      }

      int cell = hit_cell(st, mx, cr.w);
      if (cell >= 0) {
        // Activate the selected frame.
        canvas_doc_t *doc = tl_doc();
        if (doc && doc->anim && cell != doc->anim->active_frame) {
          doc_push_undo(doc);
          if (anim_timeline_switch_frame(doc->anim, cell,
                                         &doc->pixels,
                                         doc->canvas_w, doc->canvas_h,
                                         FRAME_FORMAT_INDEXED)) {
            // Sync the active layer's pixel pointer.
            if (doc->layer_count > 0)
              doc->layers[doc->active_layer]->pixels = doc->pixels;
            doc->canvas_dirty = true;
            if (doc->canvas_win) invalidate_window(doc->canvas_win);
            st->thumbs_dirty = true;
            invalidate_window(win);
          } else {
            doc_discard_undo(doc);
          }
        }
        // Begin drag.
        st->drag_active = true;
        st->drag_from   = cell;
        st->drag_x      = mx;
        set_capture(win);
      }
      return true;
    }

    case evLeftButtonUp: {
      if (!st || !st->drag_active) return true;
      set_capture(NULL);
      int mx = (int)(wparam & 0xFFFF);
      irect16_t cr = get_client_rect(win);
      int drop = hit_cell(st, mx, cr.w);
      if (drop >= 0 && drop != st->drag_from) {
        canvas_doc_t *doc = tl_doc();
        if (doc && doc->anim) {
          anim_timeline_move_frame(doc->anim, st->drag_from, drop);
          st->thumbs_dirty = true;
          invalidate_window(win);
        }
      }
      st->drag_active = false;
      st->drag_from   = -1;
      return true;
    }

    case evMouseMove: {
      if (!st) return true;
      int mx = (int)(wparam & 0xFFFF);
      irect16_t cr = get_client_rect(win);
      int cell = hit_cell(st, mx, cr.w);
      if (cell != st->hover_cell) {
        st->hover_cell = cell;
        invalidate_window(win);
      }
      return true;
    }

    case evRightButtonDown: {
      if (!st) return true;
      // Right-click: new frame as a quick action (context menus not available).
      handle_menu_command(ID_ANIM_NEW_FRAME);
      return true;
    }

    case evCommand:
      // Forward toolbar clicks to handle_menu_command.
      if (HIWORD(wparam) == tbButtonClick)
        handle_menu_command((uint16_t)LOWORD(wparam));
      return false;

    default:
      return false;
  }
}

// ============================================================
// Factory helper
// ============================================================

window_t *create_timeline_window(void) {
  if (!g_app) return NULL;

  int screen_w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int screen_h = ui_get_system_metrics(kSystemMetricScreenHeight);
  if (screen_w <= 0) screen_w = SCREEN_W;
  if (screen_h <= 0) screen_h = SCREEN_H;

  int tw_x = 0;
  int tw_y = screen_h - TIMELINE_WIN_H;
  int tw_w = screen_w;
  int tw_h = TIMELINE_WIN_H;

  window_t *tw = create_window(
      "Timeline",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(tw_x, tw_y, tw_w, tw_h),
      NULL, timeline_proc, g_app->hinstance, NULL);
  show_window(tw, true);
  g_app->timeline_win = tw;
  return tw;
}

void timeline_win_refresh(void) {
  if (!g_app || !g_app->timeline_win) return;
  timeline_state_t *st = (timeline_state_t *)g_app->timeline_win->userdata;
  if (st) st->thumbs_dirty = true;
  invalidate_window(g_app->timeline_win);
}

// ============================================================
// Animation playback tick
// ============================================================

void anim_tick(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return;
  anim_timeline_t *tl = doc->anim;
  if (!tl->playing || tl->frame_count <= 1) return;

  int next = tl->active_frame + 1;
  if (next >= tl->frame_count) {
    if (!tl->loop) { tl->playing = false; timeline_win_refresh(); return; }
    next = 0;
  }

  if (anim_timeline_switch_frame(tl, next, &doc->pixels,
                                  doc->canvas_w, doc->canvas_h,
                                  FRAME_FORMAT_INDEXED)) {
    if (doc->layer_count > 0)
      doc->layers[doc->active_layer]->pixels = doc->pixels;
    doc->canvas_dirty = true;
    if (doc->canvas_win) invalidate_window(doc->canvas_win);
    timeline_win_refresh();
  }
}

#endif // IMAGEEDITOR_ANIMATIONS
