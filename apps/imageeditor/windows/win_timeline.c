#include "imageeditor.h"
#include <orion/user/toolbar.h>

#define FRAME_ITEM_BASE 0x8000u // item idents travel in LOWORD(wparam)
#define FRAME_MAX_VISIBLE 8

typedef struct {
  int first_frame, visible_count;
  GLuint *thumbs;
  uint32_t *thumb_rev;
  int thumb_count;
  bool thumbs_dirty, thumbs_active_only;
  canvas_doc_t *last_doc;
  int last_count;
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
    free(st->thumb_rev);
    st->thumbs = NULL;
    st->thumb_rev = NULL;
    st->thumb_count = 0;
    return;
  }
  GLuint *t = realloc(st->thumbs, sizeof(GLuint) * (size_t)frame_count);
  uint32_t *rev = realloc(st->thumb_rev, sizeof(uint32_t) * (size_t)frame_count);
  if (!t || !rev) {
    return;
  }
  st->thumbs = t;
  st->thumb_rev = rev;
  for (int i = st->thumb_count; i < frame_count; i++) {
    st->thumbs[i] = 0;
    st->thumb_rev[i] = 0;
  }
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
  for (int i = 0; i < doc->anim->frame_count && i < st->thumb_count; i++) {
    uint32_t rev = doc->anim->frames[i] ? doc->anim->frames[i]->revision : 0;
    if (st->thumbs_active_only && i != doc->anim->active_frame && st->thumbs[i]) continue;
    if (!st->thumbs_dirty && st->thumbs[i] && st->thumb_rev[i] == rev) continue;
    anim_frame_t preview = *doc->anim->frames[i];
    uint8_t *rgba = NULL;
    if (pencil_has_layers(doc)) {
      rgba = malloc((size_t)doc->canvas_w * doc->canvas_h * 4);
      if (!rgba || !doc_anim_rgba(doc, i, rgba)) { free(rgba); continue; }
      preview.data = rgba; preview.data_size = (size_t)doc->canvas_w * doc->canvas_h * 4;
      preview.format = FRAME_FORMAT_RGBA;
    }
    if (anim_render_frame_thumbnail_scaled(&preview,
                                           doc->canvas_w, doc->canvas_h,
                                           TIMELINE_THUMB_W,
                                           &st->thumbs[i],
#if IMAGEEDITOR_INDEXED
                                           doc->ipal.entries
#else
                                           NULL
#endif
                                           ))
      st->thumb_rev[i] = rev;
    free(rgba);
  }
  st->thumbs_dirty = false;
  st->thumbs_active_only = false;
}

static int timeline_fixed_width(void) {
  return get_theme()->toolbar_grip_size + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH) +
         7 * (40 + TOOLBAR_SPACING) + 6 + TOOLBAR_SPACING;
}

// Inter-frame TOOLBAR_ITEM_SPACER width: the toolbar also adds TOOLBAR_SPACING
// after the previous frame and after the spacer, so spacer + 2*SPACING = GAP.
#define TIMELINE_FRAME_SPACER_W (TIMELINE_FRAME_GAP - 2 * TOOLBAR_SPACING)

static void timeline_layout(window_t *win, bool follow) {
  timeline_state_t *st = win->userdata;
  canvas_doc_t *doc = tl_doc();
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  int available = MAX(1, win->frame.w);
  st->visible_count = MIN(MAX(1, count), CLAMP((available - timeline_fixed_width()) /
                          (TIMELINE_THUMB_W + TIMELINE_FRAME_GAP), 1, FRAME_MAX_VISIBLE));
  st->first_frame = CLAMP(st->first_frame, 0, MAX(0, count - st->visible_count));
  if (follow && count) {
    int active = doc->anim->active_frame;
    if (active < st->first_frame) st->first_frame = active;
    if (active >= st->first_frame + st->visible_count) st->first_frame = active - st->visible_count + 1;
  }
}

static void timeline_build_items(window_t *win) {
  timeline_state_t *st = win->userdata;
  canvas_doc_t *doc = tl_doc();
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  toolbar_item_t items[ARRAY_LEN(kTimelineToolbar) + 2 * FRAME_MAX_VISIBLE];
  memcpy(items, kTimelineToolbar, sizeof(kTimelineToolbar));
  int n = ARRAY_LEN(kTimelineToolbar);
  items[1].icon = "play";
  items[1].checked_icon = "square";
  items[1].state = doc && doc->anim && doc->anim->playing ? TBSTATE_CHECKED : 0;
  items[3].state = g_app && g_app->anim_trace_enabled ? TBSTATE_CHECKED : 0;
  for (int i = st->first_frame; i < MIN(count, st->first_frame + st->visible_count); i++) {
    if (i > st->first_frame)
      items[n++] = (toolbar_item_t){.type = TOOLBAR_ITEM_SPACER, .w = TIMELINE_FRAME_SPACER_W};
    items[n++] = (toolbar_item_t){.type = TOOLBAR_ITEM_CUSTOM, .ident = FRAME_ITEM_BASE + i,
      .w = TIMELINE_THUMB_W, .style = TOOLBAR_ITEM_FLAG_REORDERABLE,
      .state = i == doc->anim->active_frame ? TBSTATE_CHECKED : 0, .tooltip = "Select frame; drag to reorder"};
  }
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

  anim_stop_playback(doc);

  if (doc->anim->active_frame == target_idx) {
    timeline_ensure_frame_visible(st, win, target_idx);
    invalidate_window(win);
    return true;
  }

  if (!cmd_frame_select(doc, target_idx)) return false;
  st->thumbs_dirty = true;
  st->thumbs_active_only = false;
  timeline_ensure_frame_visible(st, win, target_idx);
  timeline_toolbar_sync();
  invalidate_window(win);
  return true;
}

static void timeline_draw_frame(window_t *win, int idx, toolbar_draw_item_t *draw) {
  timeline_state_t *st = win->userdata;
  canvas_doc_t *doc = tl_doc();
  bool playing = doc && doc->anim && doc->anim->playing;
  if (st->thumbs_dirty && (!playing || !st->thumbs))
    rebuild_thumbnails(st);
  irect16_t inner = draw->rect;
  bool selected = (draw->state & CTRL_SELECTED) != 0;
  bool outlined = selected || (draw->state & CTRL_HOVER) != 0;
  int radius = 4;
  if (outlined) {
    fill_rounded_rect(get_sys_color(selected ? brAccent : brToolbarForeground),
                      rect_inset(inner, -TIMELINE_FRAME_OUTLINE),
                      radius + TIMELINE_FRAME_OUTLINE);
  }
  fill_rounded_rect(doc && doc->background.show ? doc->background.color : MAKE_COLOR(0xCC, 0xCC, 0xCC, 0xFF), inner, radius);
  if (idx >= 0 && idx < st->thumb_count && st->thumbs[idx])
    draw_rounded_rect(st->thumbs[idx], inner, inner.w, inner.h, radius+2, 2.0f);
  char number[16];
  snprintf(number, sizeof(number), "%d", idx + 1);
  int h = text_char_height(FONT_SMALLEST), w = text_strwidth(FONT_SMALLEST, number);
  irect16_t badge = R(inner.x + 2, inner.y + inner.h - h - 3, w + 6, h + 2);
  uint32_t badge_bg = selected ? get_sys_color(brAccent) : MAKE_COLOR(0x2A, 0x2A, 0x2A, 0xFF);
  uint32_t badge_fg = selected ? get_sys_color(brActiveTitlebarText) : MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF);
  fill_rounded_rect(badge_bg, badge, 3);
  draw_text(FONT_SMALLEST, number, badge.x + 3, badge.y + 1, badge_fg);
}

static result_t timeline_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  timeline_state_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      st->thumbs_dirty = true;
      st->thumbs_active_only = false;
      send_message(win, tbSetStyle, TOOLBAR_STYLE_GRIP, NULL);
      return true;
    case evPaint: return true;
    case evDestroy:
      if (st) {
        anim_render_shutdown();
        for (int i = 0; i < st->thumb_count; i++) R_DeleteTexture(st->thumbs[i]);
        free(st->thumbs);
        free(st->thumb_rev);
      }
      if (g_app && g_app->timeline_win == win) g_app->timeline_win = NULL;
      return true;
    case evResize:
      timeline_layout(win, true);
      timeline_build_items(win);
      return true;
    case evClose:
      show_window(win, false);
      return true;
    case tbDrawItem:
      if (wparam >= FRAME_ITEM_BASE && lparam) timeline_draw_frame(win, wparam - FRAME_ITEM_BASE, lparam);
      return true;
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        uint32_t ident = LOWORD(wparam);
        if (ident >= FRAME_ITEM_BASE) return timeline_select_frame(win, st, ident - FRAME_ITEM_BASE);
        if (ident == ID_ANIM_PLAY && tl_doc() && tl_doc()->anim && tl_doc()->anim->playing)
          handle_menu_command(ID_ANIM_STOP);
        else handle_menu_command(ident);
        return true;
      }
      if (HIWORD(wparam) == tbItemDrop && lparam) {
        toolbar_drop_item_t *drop = lparam;
        canvas_doc_t *doc = tl_doc();
        int from = (int)(drop->from_ident - FRAME_ITEM_BASE), to = (int)(drop->to_ident - FRAME_ITEM_BASE);
        if (!doc || !doc->anim || from < 0 || to < 0 || from >= doc->anim->frame_count || to >= doc->anim->frame_count) return false;
        cmd_frame_move(doc, from, to);
        return true;
      }
      return false;
    case evWheel: {
      int dx = (int16_t)LOWORD((uintptr_t)lparam), dy = (int16_t)HIWORD((uintptr_t)lparam);
      int delta = dx ? dx : -dy;
      if (!delta) return true;
      st->first_frame += delta > 0 ? 1 : -1;
      timeline_layout(win, false);
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

    default: return false;
  }
}

window_t *create_timeline_window(void) {
  if (!g_app) return NULL;
  window_t *win = create_window("Frames", WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON |
    WINDOW_NORESIZE | WINDOW_NOTITLE | WINDOW_TOOLBAR,
    MAKERECT(0, 0, 500, TIMELINE_WIN_H), g_app->chrome_win, timeline_proc, g_app->hinstance, NULL);
  if (!win) return NULL;
  g_app->timeline_win = win;
  if (win->parent) dock_window(win, DOCK_BOTTOM, DOCK_EDGE(DOCK_TOP) | DOCK_EDGE(DOCK_BOTTOM), DOCK_TOOLBAR, 0, 0);
  timeline_toolbar_sync();
  show_window(win, IMAGEEDITOR_BW || (tl_doc() && tl_doc()->anim && tl_doc()->anim->frame_count > 1));
  return win;
}

void timeline_toolbar_sync(void) {
  if (!g_app || !g_app->timeline_win) return;
  window_t *win = g_app->timeline_win;
  timeline_layout(win, true);
  timeline_build_items(win);
}

void timeline_win_refresh(void) {
  if (!g_app || !g_app->timeline_win) return;
  timeline_state_t *st = g_app->timeline_win->userdata;
  canvas_doc_t *doc = tl_doc();
  if (st && !(doc && doc->anim && doc->anim->playing)) {
    st->thumbs_dirty = true;
    st->thumbs_active_only = false;
  }
  timeline_toolbar_sync();
  int count = doc && doc->anim ? doc->anim->frame_count : 0;
  if (st && count > 1 && (st->last_doc != doc || st->last_count <= 1))
    show_window(g_app->timeline_win, true);
  if (st) { st->last_doc = doc; st->last_count = count; }
  invalidate_window(g_app->timeline_win);
}

void timeline_win_refresh_active_frame(void) {
  if (!g_app || !g_app->timeline_win) return;
  timeline_state_t *st = g_app->timeline_win->userdata;
  canvas_doc_t *doc = tl_doc();
  if (!st || !doc || !doc->anim || st->last_doc != doc ||
      st->last_count != doc->anim->frame_count) { timeline_win_refresh(); return; }
  if (!st->thumbs_dirty) st->thumbs_active_only = true;
  st->thumbs_dirty = true;
  timeline_toolbar_sync();
  invalidate_window(g_app->timeline_win);
}

void anim_tick(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return;
  anim_timeline_t *tl = doc->anim;
  if (!tl->playing || tl->frame_count < 1) return;
  int old_delay = tl->frames[tl->active_frame]->delay_ms;

  int next = tl->active_frame + 1;
  if (next >= tl->frame_count) {
    if (!tl->loop) {
      anim_stop_playback(doc);
      return;
    }
    next = 0;
  }

  if (doc_anim_load(doc, next)) {
    if (doc->layer.count > 0)
      doc->layer.stack[doc->layer.active]->pixels = doc->pixels;
    doc->canvas_dirty = true;
    int delay = tl->frames[next]->delay_ms;
    if (delay != old_delay && delay > 0 && g_app && g_app->anim_timer_id && g_app->timeline_win) {
      axCancelTimer(g_app->anim_timer_id);
      g_app->anim_timer_id = axSetTimer(g_app->timeline_win, delay, NULL, (bool_t)1);
      if (!g_app->anim_timer_id) {
        anim_stop_playback(doc);
      }
    }
    if (doc->canvas_win) invalidate_window(doc->canvas_win);
    timeline_win_refresh();
  }
}
