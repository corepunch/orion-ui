#include "imageeditor.h"
#include <orion/user/toolbar.h>

#define FRAME_ITEM_BASE 0x10000u
#define FRAME_MAX_VISIBLE 8
#define FRAME_MARGIN 12
#define FRAME_COUNTER 0x7000

typedef struct {
  int first_frame, visible_count, screen_w, screen_h;
  ipoint16_t last_position;
  bool user_placed;
  GLuint *thumbs;
  int thumb_count;
  bool thumbs_dirty, positioned;
  canvas_doc_t *last_doc;
  int last_count;
  char counter[48];
} timeline_state_t;

static const toolbar_item_t kTimelineToolbar[] = {
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_PREV_FRAME, .icon = "fast-arrow-left", .w = 40, .tooltip = "Previous frame"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_PLAY, .icon = "play", .w = 40, .tooltip = "Play / stop"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_NEXT_FRAME, .icon = "fast-arrow-right", .w = 40, .tooltip = "Next frame"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_TRACE, .icon = "eye", .w = 40, .tooltip = "Onion skin"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_NEW_FRAME, .icon = "plus", .w = 40, .tooltip = "New frame"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_DUPLICATE_FRAME, .icon = "copy", .w = 40, .tooltip = "Duplicate frame"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_ANIM_DELETE_FRAME, .icon = "trash", .w = 40, .tooltip = "Delete frame"},
  {.type = TOOLBAR_ITEM_SPACER, .w = 6},
};

static canvas_doc_t *tl_doc(void) {
  return g_app ? g_app->active_doc : NULL;
}

static void sync_thumb_array(timeline_state_t *st, int frame_count) {
  if (frame_count == st->thumb_count) return;

  if (frame_count < st->thumb_count) {
    for (int i = frame_count; i < st->thumb_count; i++) {
      if (st->thumbs[i]) {
        R_DeleteTexture(st->thumbs[i]);
        st->thumbs[i] = 0;
      }
    }
  }

  if (!frame_count) {
    free(st->thumbs);
    st->thumbs = NULL;
    st->thumb_count = 0;
    return;
  }
  GLuint *t = realloc(st->thumbs, sizeof(GLuint) * (size_t)frame_count);
  if (!t) {
    IE_TRACE("frames thumbnail allocation failed count=%d", frame_count);
    return;
  }
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
      anim_render_frame_thumbnail_scaled(doc->anim->frames[i],
                                         doc->canvas_w, doc->canvas_h,
                                         TIMELINE_THUMB_W,
                                         &st->thumbs[i],
#if IMAGEEDITOR_INDEXED
                                   doc->ipal.entries
#else
                                   NULL
#endif
                                   );
  }
  st->thumbs_dirty = false;
}

static int timeline_counter_width(canvas_doc_t *doc) {
  char counter[48];
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  int active = count ? doc->anim->active_frame + 1 : 0;
  snprintf(counter, sizeof(counter), "%d / %d", active, count);
  return MAX(32, text_strwidth(FONT_SMALL, counter) + 8);
}

static int timeline_fixed_width(canvas_doc_t *doc) {
  return TOOLBAR_GRIP_WIDTH + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH) +
         7 * (40 + TOOLBAR_SPACING) + 6 + TOOLBAR_SPACING + timeline_counter_width(doc);
}

static void timeline_layout(window_t *win, int width, int height, bool follow) {
  timeline_state_t *st = win->userdata;
  canvas_doc_t *doc = tl_doc();
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  if (width <= 0) width = st->screen_w > 0 ? st->screen_w : SCREEN_W;
  if (height <= 0) height = st->screen_h > 0 ? st->screen_h : SCREEN_H;
  st->screen_w = width;
  st->screen_h = height;
  int available = MAX(1, width - APP_TOOLS_W - 2 * FRAME_MARGIN);
  st->visible_count = MIN(MAX(1, count), CLAMP((available - timeline_fixed_width(doc)) /
                          (TIMELINE_THUMB_W + TOOLBAR_SPACING), 1, FRAME_MAX_VISIBLE));
  st->first_frame = CLAMP(st->first_frame, 0, MAX(0, count - st->visible_count));
  if (follow && count) {
    int active = doc->anim->active_frame;
    if (active < st->first_frame) st->first_frame = active;
    if (active >= st->first_frame + st->visible_count) st->first_frame = active - st->visible_count + 1;
  }
  int w = timeline_fixed_width(doc) + st->visible_count * (TIMELINE_THUMB_W + TOOLBAR_SPACING);
  int x = win->frame.x, y = win->frame.y;
  if (st->positioned && (x != st->last_position.x || y != st->last_position.y)) st->user_placed = true;
  if (!st->user_placed) {
    x = APP_TOOLS_W + (width - APP_TOOLS_W - w) / 2;
    y = height - TIMELINE_WIN_H - FRAME_MARGIN;
  }
  x = CLAMP(x, 0, MAX(0, width - w));
  y = CLAMP(y, 0, MAX(0, height - TIMELINE_WIN_H));
  if (win->frame.w != w) resize_window(win, w, TIMELINE_WIN_H);
  if (win->frame.x != x || win->frame.y != y) move_window(win, x, y);
  st->positioned = true;
  st->last_position = (ipoint16_t){x, y};
}

static void timeline_build_items(window_t *win) {
  timeline_state_t *st = win->userdata;
  canvas_doc_t *doc = tl_doc();
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  toolbar_item_t items[ARRAY_LEN(kTimelineToolbar) + FRAME_MAX_VISIBLE + 1];
  memcpy(items, kTimelineToolbar, sizeof(kTimelineToolbar));
  int n = ARRAY_LEN(kTimelineToolbar);
  items[1].icon = doc && doc->anim && doc->anim->playing ? "square" : "play";
  items[3].flags = g_app && g_app->anim_trace_enabled ? TOOLBAR_BUTTON_FLAG_ACTIVE : 0;
  for (int i = st->first_frame; i < MIN(count, st->first_frame + st->visible_count); i++)
    items[n++] = (toolbar_item_t){.type = TOOLBAR_ITEM_CUSTOM, .ident = FRAME_ITEM_BASE + i,
      .w = TIMELINE_THUMB_W, .flags = TOOLBAR_ITEM_FLAG_REORDERABLE |
        (i == doc->anim->active_frame ? TOOLBAR_BUTTON_FLAG_ACTIVE : 0), .tooltip = "Select frame; drag to reorder"};
  snprintf(st->counter, sizeof(st->counter), "%d / %d", count ? doc->anim->active_frame + 1 : 0, count);
  items[n++] = (toolbar_item_t){.type = TOOLBAR_ITEM_LABEL, .ident = FRAME_COUNTER,
                                .w = timeline_counter_width(doc), .text = st->counter};
  send_message(win, tbSetItems, n, items);
}

static void timeline_ensure_frame_visible(timeline_state_t *st, window_t *win, int frame_idx) {
  if (frame_idx < st->first_frame) st->first_frame = frame_idx;
  if (frame_idx >= st->first_frame + st->visible_count) st->first_frame = frame_idx - st->visible_count + 1;
  timeline_build_items(win);
}

static bool timeline_select_frame(window_t *win, timeline_state_t *st,
                                  int target_idx) {
  canvas_doc_t *doc = tl_doc();
  if (!win || !st || !doc || !doc->anim) return false;
  if (target_idx < 0 || target_idx >= doc->anim->frame_count) return false;

  IE_TRACE("frame select win=%p index=%d old=%d count=%d", (void *)win, target_idx, doc->anim->active_frame, doc->anim->frame_count);
  anim_stop_playback(doc);

  if (doc->anim->active_frame == target_idx) {
    timeline_ensure_frame_visible(st, win, target_idx);
    invalidate_window(win);
    return true;
  }

  doc_push_undo(doc);
  if (!anim_timeline_switch_frame(doc->anim, target_idx,
                                  &doc->pixels,
                                  doc->canvas_w, doc->canvas_h,
                                  IE_FRAME_FORMAT)) {
    doc_discard_undo(doc);
    return false;
  }

  if (doc->layer.count > 0)
    doc->layer.stack[doc->layer.active]->pixels = doc->pixels;
  doc->canvas_dirty = true;
  if (doc->canvas_win) invalidate_window(doc->canvas_win);
  st->thumbs_dirty = true;
  timeline_ensure_frame_visible(st, win, target_idx);
  timeline_toolbar_sync();
  invalidate_window(win);
  return true;
}

static void timeline_draw_frame(window_t *win, int idx, toolbar_draw_item_t *draw) {
  timeline_state_t *st = win->userdata;
  canvas_doc_t *doc = tl_doc();
  if (st->thumbs_dirty) rebuild_thumbnails(st);
  irect16_t outer = rect_inset(draw->rect, 2);
  irect16_t inner = rect_inset(outer, 2);
  uint32_t border = get_sys_color((draw->state & CTRL_SELECTED) ? brAccent : brControlBg);
  if (!(draw->state & CTRL_SELECTED) && (draw->state & CTRL_HOVER)) border = get_sys_color(brToolbarForeground);
  fill_rounded_rect(border, outer, 7);
  fill_rounded_rect(doc && doc->background.show ? doc->background.color : MAKE_COLOR(0xCC, 0xCC, 0xCC, 0xFF), inner, 5);
  if (idx >= 0 && idx < st->thumb_count && st->thumbs[idx])
    draw_rounded_rect(st->thumbs[idx], inner, inner.w, inner.h, 5, 1.0f);
  char number[16];
  snprintf(number, sizeof(number), "%d", idx + 1);
  int h = text_char_height(FONT_SMALLEST), w = text_strwidth(FONT_SMALLEST, number);
  irect16_t badge = R(inner.x + 2, inner.y + inner.h - h - 3, w + 6, h + 2);
  fill_rounded_rect(MAKE_COLOR(0x2A, 0x2A, 0x2A, 0xFF), badge, 3);
  draw_text(FONT_SMALLEST, number, badge.x + 3, badge.y + 1, MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
}

static result_t timeline_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  timeline_state_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      st->thumbs_dirty = true;
      send_message(win, tbSetButtonSize, TIMELINE_CLIENT_H, NULL);
      send_message(win, tbSetStyle, TOOLBAR_STYLE_GRIP, NULL);
      return true;
    case evPaint: return true;
    case evDestroy:
      if (st) {
        anim_render_shutdown();
        for (int i = 0; i < st->thumb_count; i++) R_DeleteTexture(st->thumbs[i]);
        free(st->thumbs);
      }
      if (g_app && g_app->timeline_win == win) g_app->timeline_win = NULL;
      return true;
    case evDisplayChange:
      IE_TRACE("frames display win=%p size=%ux%u", (void *)win, LOWORD(wparam), HIWORD(wparam));
      timeline_layout(win, LOWORD(wparam), HIWORD(wparam), true);
      timeline_build_items(win);
      return true;
    case evClose:
      IE_TRACE("frames hide win=%p", (void *)win);
      show_window(win, false);
      return true;
    case tbDrawItem:
      if (wparam >= FRAME_ITEM_BASE && lparam) timeline_draw_frame(win, wparam - FRAME_ITEM_BASE, lparam);
      return true;
    case evCommand:
      if (HIWORD(wparam) == tbItemDrop && lparam) {
        toolbar_drop_item_t *drop = lparam;
        canvas_doc_t *doc = tl_doc();
        int from = (int)(drop->from_ident - FRAME_ITEM_BASE), to = (int)(drop->to_ident - FRAME_ITEM_BASE);
        if (!doc || !doc->anim || from < 0 || to < 0 || from >= doc->anim->frame_count || to >= doc->anim->frame_count) return false;
        IE_TRACE("frame reorder win=%p from=%d to=%d count=%d", (void *)win, from, to, doc->anim->frame_count);
        anim_stop_playback(doc);
        anim_timeline_move_frame(doc->anim, from, to);
        ie_doc_mark_dirty(doc);
        ie_doc_update_title(doc);
        timeline_win_refresh();
        return true;
      }
      return false;
    case evWheel: {
      int dx = (int16_t)LOWORD((uintptr_t)lparam), dy = (int16_t)HIWORD((uintptr_t)lparam);
      int delta = dx ? dx : -dy;
      if (!delta) return true;
      st->first_frame += delta > 0 ? 1 : -1;
      timeline_layout(win, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight), false);
      IE_TRACE("frames scroll win=%p first=%d", (void *)win, st->first_frame);
      timeline_build_items(win);
      return true;
    }
    case evTimer:
      if (tl_doc() && tl_doc()->anim && tl_doc()->anim->playing) anim_tick(tl_doc());
      return true;
    case evKeyDown: {
      if (!st) return false;
      canvas_doc_t *doc = tl_doc();
      if (!doc || !doc->anim) return false;

      int target = doc->anim->active_frame;
      switch (wparam) {
        case AX_KEY_LEFTARROW:
        case AX_KEY_UPARROW:
          target--;
          break;
        case AX_KEY_RIGHTARROW:
        case AX_KEY_DOWNARROW:
          target++;
          break;
        case AX_KEY_HOME:
          target = 0;
          break;
        case AX_KEY_END:
          target = doc->anim->frame_count - 1;
          break;
        case AX_KEY_ENTER:
        case AX_KEY_KP_ENTER:
        case AX_KEY_SPACE:
          handle_menu_command(doc->anim->playing ? ID_ANIM_STOP : ID_ANIM_PLAY);
          return true;
        default:
          return false;
      }

      if (target < 0) target = 0;
      if (target >= doc->anim->frame_count)
        target = doc->anim->frame_count - 1;
      return timeline_select_frame(win, st, target);
    }

    case tbButtonClick:
      IE_TRACE("frames click win=%p ident=%u", (void *)win, wparam);
      if (wparam >= FRAME_ITEM_BASE) return timeline_select_frame(win, st, wparam - FRAME_ITEM_BASE);
      if (wparam == ID_ANIM_PLAY && tl_doc() && tl_doc()->anim && tl_doc()->anim->playing)
        handle_menu_command(ID_ANIM_STOP);
      else handle_menu_command(wparam);
      return true;
    default: return false;
  }
}

window_t *create_timeline_window(void) {
  if (!g_app) return NULL;
  window_t *win = create_window("Frames", WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON |
    WINDOW_NORESIZE | WINDOW_NOTITLE | WINDOW_TOOLBAR,
    MAKERECT(0, 0, 500, TIMELINE_WIN_H), NULL, timeline_proc, g_app->hinstance, NULL);
  if (!win) return NULL;
  g_app->timeline_win = win;
  timeline_toolbar_sync();
  show_window(win, IMAGEEDITOR_BW || (tl_doc() && tl_doc()->anim && tl_doc()->anim->frame_count > 1));
  return win;
}

void timeline_toolbar_sync(void) {
  if (!g_app || !g_app->timeline_win) return;
  window_t *win = g_app->timeline_win;
  timeline_layout(win, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight), true);
  timeline_build_items(win);
}

void timeline_win_refresh(void) {
  if (!g_app || !g_app->timeline_win) return;
  timeline_state_t *st = g_app->timeline_win->userdata;
  if (st) st->thumbs_dirty = true;
  timeline_toolbar_sync();
  canvas_doc_t *doc = tl_doc();
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  if (st && count > 1 && (st->last_doc != doc || st->last_count <= 1))
    show_window(g_app->timeline_win, true);
  if (st) { st->last_doc = doc; st->last_count = count; }
  invalidate_window(g_app->timeline_win);
}

void anim_tick(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return;
  anim_timeline_t *tl = doc->anim;
  if (!tl->playing || tl->frame_count < 1) return;

  int next = tl->active_frame + 1;
  if (next >= tl->frame_count) {
    if (!tl->loop) {
      anim_stop_playback(doc);
      return;
    }
    next = 0;
  }

  if (anim_timeline_switch_frame(tl, next, &doc->pixels,
                                  doc->canvas_w, doc->canvas_h,
                                  IE_FRAME_FORMAT)) {
    if (doc->layer.count > 0)
      doc->layer.stack[doc->layer.active]->pixels = doc->pixels;
    doc->canvas_dirty = true;
    if (doc->canvas_win) invalidate_window(doc->canvas_win);
    timeline_win_refresh();
  }
}
