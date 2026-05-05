// Timeline palette window — horizontal strip showing animation frames.

#include "imageeditor.h"

// ============================================================
// Layout constants
// ============================================================

// Frame thumbnail cells.
#define CELL_PAD       3     // padding inside each cell
#define CELL_NAME_H   10     // height of the name label below the thumbnail
#define CELL_H        (TIMELINE_CLIENT_H)
#define THUMB_H       (CELL_H - CELL_PAD*2 - CELL_NAME_H - 2)

// Colours
#define COL_TL_BG      MAKE_COLOR(0x2A, 0x2A, 0x2A, 0xFF)
#define COL_TL_BORDER  MAKE_COLOR(0x44, 0x44, 0x44, 0xFF)
#define COL_ACTIVE_BG  MAKE_COLOR(0x00, 0x78, 0xD7, 0xFF)
#define COL_HOVER_BG   MAKE_COLOR(0x3A, 0x3A, 0x3A, 0xFF)

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

// Toolbar ops shown in the timeline titlebar band.
static const toolbar_item_t kTimelineToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, ID_ANIM_PREV_FRAME,  sysicon_anim_frame_move_left,  0, 0, "Previous Frame" },
  { TOOLBAR_ITEM_BUTTON, ID_ANIM_NEXT_FRAME,  sysicon_anim_frame_move_right, 0, 0, "Next Frame" },
  { TOOLBAR_ITEM_SPACER, 0, -1, 6, 0, NULL },
  { TOOLBAR_ITEM_BUTTON, ID_ANIM_PLAY,        sysicon_clock_play,            0, 0, "Play" },
  { TOOLBAR_ITEM_BUTTON, ID_ANIM_NEW_FRAME,   sysicon_anim_frame_add,       0, 0, "New Frame" },
  { TOOLBAR_ITEM_BUTTON, ID_ANIM_DELETE_FRAME,sysicon_anim_frame_delete,    0, 0, "Delete Frame" },
};

// ============================================================
// Helpers
// ============================================================

static canvas_doc_t *tl_doc(void) {
  return g_app ? g_app->active_doc : NULL;
}

// x offset of cell `n` in client space, accounting for horizontal scroll.
static int cell_x(const timeline_state_t *st, int n) {
  return n * TIMELINE_THUMB_W - st->scroll_x;
}

// Hit-test: which frame cell is at client x?
static int hit_cell(const timeline_state_t *st, int cx, int w) {
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
  if (!doc || !doc->anim) {
    // No active animation — release any stale thumbnails and stop retrying.
    sync_thumb_array(st, 0);
    st->thumbs_dirty = false;
    return;
  }
  sync_thumb_array(st, doc->anim->frame_count);
  for (int i = 0; i < doc->anim->frame_count; i++) {
    if (i < st->thumb_count)
      anim_render_frame_thumbnail(doc->anim->frames[i],
                                  doc->canvas_w, doc->canvas_h,
                                  &st->thumbs[i]);
  }
  st->thumbs_dirty = false;
}

static void update_play_button_icon(window_t *win) {
  if (!win || !g_app) return;
  window_t *btn = get_window_item(win, ID_ANIM_PLAY);
  bitmap_strip_t *strip = ui_get_sysicon_strip();
  if (!btn || !strip) return;

  canvas_doc_t *doc = tl_doc();
  int icon = (doc && doc->anim && doc->anim->playing)
           ? (sysicon_clock_stop - SYSICON_BASE)
           : (sysicon_clock_play - SYSICON_BASE);
  send_message(btn, btnSetImage, (uint32_t)icon, strip);
}

// Draw a single frame cell at position cx in client space.
static void draw_cell(const timeline_state_t *st, canvas_doc_t *doc, int idx,
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
    if (tw > 0 && th > 0) {
      if (doc && doc->show_background)
        fill_rect(doc->background_color, R(tx, ty, tw, th));
      else
        draw_checkerboard(R(tx, ty, tw, th), CANVAS_CHECKER_SQUARE_PX);
      draw_rect(st->thumbs[idx], R(tx, ty, tw, th));
    }
  }

  // Frame number label.
  char num_buf[8];
  snprintf(num_buf, sizeof(num_buf), "%d", idx + 1);
  int nx = cx + cw / 2 - (int)strlen(num_buf) * 3;
  int ny = h - CELL_NAME_H - 1;
  draw_text_small(num_buf, nx, ny,
                  active ? MAKE_COLOR(0xFF,0xFF,0xFF,0xFF)
                         : MAKE_COLOR(0x88,0x88,0x88,0xFF));
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
      send_message(win, tbSetItems,
                   (uint32_t)(sizeof(kTimelineToolbar) / sizeof(kTimelineToolbar[0])),
                   (void *)kTimelineToolbar);
      update_play_button_icon(win);
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

      irect16_t cr = get_client_rect(win);
      int h = cr.h;
      int w = cr.w;
      fill_rect(COL_TL_BG, R(0, 0, w, h));

      if (st->thumbs_dirty)
        rebuild_thumbnails(st);

      canvas_doc_t *doc = tl_doc();
      if (doc && doc->anim) {
        int active = doc->anim->active_frame;
        for (int i = 0; i < doc->anim->frame_count; i++) {
          int cx = cell_x(st, i);
          if (cx + TIMELINE_THUMB_W < 0) continue;
          if (cx > w) break;
          draw_cell(st, doc, i, i == active, i == st->hover_cell, h);
        }
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!st) return true;
      int mx = (int)(wparam & 0xFFFF);
      irect16_t cr = get_client_rect(win);

      int cell = hit_cell(st, mx, cr.w);
      if (cell >= 0) {
        // Activate the selected frame.
        canvas_doc_t *doc = tl_doc();
        if (doc && doc->anim && cell != doc->anim->active_frame) {
          doc_push_undo(doc);
          if (anim_timeline_switch_frame(doc->anim, cell,
                                         &doc->pixels,
                                         doc->canvas_w, doc->canvas_h,
                                         FRAME_FORMAT_RGBA)) {
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

    case evWheel: {
      // Scroll the timeline horizontally.  The wheel delta is packed as
      // MAKEDWORD(dx * sensitivity, dy * sensitivity) by the platform layer.
      // For a horizontal strip, treat vertical scroll (dy) as horizontal too.
      // Wheel-up (dy > 0) decreases scroll_x (scrolls timeline left,
      // revealing earlier frames); wheel-down increases it (later frames).
      if (!st) return true;
      int16_t dy = (int16_t)(wparam >> 16);
      int delta = -(int)dy; // negate: wheel-up → scroll left (negative offset)
      if (delta == 0) return true;
      irect16_t cr = get_client_rect(win);
      canvas_doc_t *doc = tl_doc();
      int content_w = doc && doc->anim
                      ? doc->anim->frame_count * TIMELINE_THUMB_W
                      : 0;
      int viewport_w = cr.w;
      int max_scroll = content_w - viewport_w;
      if (max_scroll < 0) max_scroll = 0;
      st->scroll_x += delta;
      if (st->scroll_x < 0) st->scroll_x = 0;
      if (st->scroll_x > max_scroll) st->scroll_x = max_scroll;
      invalidate_window(win);
      return true;
    }

    case evTimer: {
      // Advance playback by one frame.
      canvas_doc_t *doc = tl_doc();
      if (doc && doc->anim && doc->anim->playing)
        anim_tick(doc);
      return true;
    }

    case tbButtonClick:
      if (wparam == ID_ANIM_PLAY) {
        canvas_doc_t *doc = tl_doc();
        handle_menu_command(doc && doc->anim && doc->anim->playing
                            ? ID_ANIM_STOP : ID_ANIM_PLAY);
      } else {
        handle_menu_command((uint16_t)wparam);
      }
      return true;

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
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(tw_x, tw_y, tw_w, tw_h),
      NULL, timeline_proc, g_app->hinstance, NULL);
  show_window(tw, true);
  g_app->timeline_win = tw;
  return tw;
}

void timeline_toolbar_sync(void) {
  if (!g_app || !g_app->timeline_win) return;
  update_play_button_icon(g_app->timeline_win);
}

void timeline_win_refresh(void) {
  if (!g_app || !g_app->timeline_win) return;
  timeline_state_t *st = (timeline_state_t *)g_app->timeline_win->userdata;
  if (st) st->thumbs_dirty = true;
  timeline_toolbar_sync();
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
    if (!tl->loop) {
      tl->playing = false;
      if (g_app && g_app->anim_timer_id) {
        axCancelTimer(g_app->anim_timer_id);
        g_app->anim_timer_id = 0;
      }
      timeline_win_refresh();
      return;
    }
    next = 0;
  }

  if (anim_timeline_switch_frame(tl, next, &doc->pixels,
                                  doc->canvas_w, doc->canvas_h,
                                  FRAME_FORMAT_RGBA)) {
    if (doc->layer_count > 0)
      doc->layers[doc->active_layer]->pixels = doc->pixels;
    doc->canvas_dirty = true;
    if (doc->canvas_win) invalidate_window(doc->canvas_win);
    timeline_win_refresh();
  }
}
