#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/ui.h>
#include "agent_icon.h"

#define ICON_STATUS_SIZE 9
#define ICON_STATUS_GAP 4
#define ICON_BADGE_TEXT_MAX 15

static ipoint16_t aim_mouse_point(uint32_t packed) {
  return (ipoint16_t){(int16_t)LOWORD(packed), (int16_t)HIWORD(packed)};
}

static void aim_select(window_t *win, bool selected, bool notify) {
  bool changed = (win->value != 0) != selected;
  if (selected && win->parent) {
    for (window_t *it = win->parent->children; it; it = it->next) {
      if (it != win && it->proc == win_agent_icon && it->value) {
        it->value = false; invalidate_window(it);
      }
    }
  }
  win->value = selected;
  if (changed) {
    invalidate_window(win);
    if (notify) {
      window_t *target = win->parent ? win->parent : get_root_window(win);
      send_message(target, evCommand, MAKEDWORD(win->id, icnSelectionChange), win);
    }
  }
}

static void aim_notify(window_t *win, uint16_t code) {
  agent_icon_state_t *st = (agent_icon_state_t *)win->userdata2;
  window_t *target = (st && st->notify_window && is_window(st->notify_window)) ? st->notify_window
                     : (win->parent ? win->parent : get_root_window(win));
  send_message(target, evCommand, MAKEDWORD(win->id, code), win);
}

static irect16_t aim_image_rect(window_t *win, const agent_icon_state_t *st) {
  int status_h = st->status_texture ? ICON_STATUS_SIZE : 0;
  int label_h = MAX(text_char_height("Caption"), status_h);
  int left_strip = st->input_count ? AGENT_ICON_STRIP_W : 0;
  int right_strip = st->output_count ? AGENT_ICON_STRIP_W : 0;
  irect16_t local = get_client_rect(win);
  irect16_t area = rect_trim_left(rect_trim_right(rect_trim_bottom(local, label_h), right_strip), left_strip);
  int avail_w = MAX(1, area.w), avail_h = MAX(1, area.h);
  int w = avail_w, h = avail_h;
  if (st->image_w > 0 && st->image_h > 0) {
    float scale = MIN((float)avail_w / st->image_w, (float)avail_h / st->image_h);
    w = MAX(1, (int)(st->image_w * scale));
    h = MAX(1, (int)(st->image_h * scale));
  }
  return rect_center(area, w, h);
}

static irect16_t aim_output_rect(window_t *win, const agent_icon_state_t *st, int index) {
  irect16_t image = aim_image_rect(win, st);
  int status_h = st->status_texture ? ICON_STATUS_SIZE : 0;
  int label_h = MAX(text_char_height("Caption"), status_h);
  int area_h = MAX(AGENT_ICON_ARTIFACT_SIZE, rect_trim_bottom(get_client_rect(win), label_h).h);
  int size = MIN(AGENT_ICON_ARTIFACT_SIZE, MAX(16, area_h / MAX(1, st->output_count)));
  int total_h = st->output_count * size;
  int x = image.x + image.w - size / 2;
  int y = MAX(0, (area_h - total_h) / 2) + index * size;
  return R(x, y, size, size);
}

static irect16_t aim_input_rect(window_t *win, const agent_icon_state_t *st, int index) {
  irect16_t image = aim_image_rect(win, st);
  int status_h = st->status_texture ? ICON_STATUS_SIZE : 0;
  int label_h = MAX(text_char_height("Caption"), status_h);
  int area_h = MAX(AGENT_ICON_ARTIFACT_SIZE, rect_trim_bottom(get_client_rect(win), label_h).h);
  int size = MIN(AGENT_ICON_ARTIFACT_SIZE, MAX(16, area_h / MAX(1, st->input_count)));
  int total_h = st->input_count * size;
  int x = image.x - size / 2;
  int y = MAX(0, (area_h - total_h) / 2) + index * size;
  return R(x, y, size, size);
}

static void aim_hide_ghost(agent_icon_state_t *st) {
  if (!st || !st->ghost) return;
  if (is_window(st->ghost)) destroy_window(st->ghost);
  st->ghost = NULL;
}

static void aim_update_ghost(window_t *win, agent_icon_state_t *st, ipoint16_t point) {
  if (!st || st->artifact_pending_col < 0) return;
  irect16_t slot = st->artifact_pending_col == 0
    ? aim_input_rect(win, st, st->artifact_pending_idx)
    : aim_output_rect(win, st, st->artifact_pending_idx);
  int sx = window_screen_x(win) + point.x - win->hscroll.pos;
  int sy = window_screen_y(win) + point.y - win->vscroll.pos;
  if (!st->ghost) {
    st->ghost = create_window("",
      WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON |
      WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_TRANSPARENT,
      MAKERECT(sx - st->artifact_grab_x, sy - st->artifact_grab_y, slot.w, slot.h),
      NULL, win_agent_icon, win->hinstance, st);
    if (!st->ghost) return;
  }
  move_window(st->ghost, sx - st->artifact_grab_x, sy - st->artifact_grab_y);
  resize_window(st->ghost, slot.w, slot.h);
  show_window(st->ghost, true); invalidate_window(st->ghost);
}

static window_t *aim_drop_target(window_t *win, ipoint16_t point) {
  if (!win->parent) return NULL;
  ipoint16_t parent_point = {(int16_t)(win->frame.x + point.x), (int16_t)(win->frame.y + point.y)};
  for (window_t *it = win->parent->children; it; it = it->next)
    if (it != win && it->proc == win_agent_icon && rect_contains_point(it->frame, parent_point)) return it;
  return NULL;
}

static void aim_draw_badge(window_t *win, irect16_t image, const char *text,
                           uint32_t bg, uint32_t fg, int anchor, int stack) {
  if (!text || !text[0]) return;
  int h = text_char_height("Caption") + 4;
  int w = text_strwidth("Caption", text) + 8;
  int x = anchor == 0 ? image.x + (image.w - w) / 2
        : (anchor == 1 || anchor == 3) ? image.x - 2 : image.x + image.w - w + 2;
  int y = (anchor == 0 || anchor == 1 || anchor == 4)
          ? image.y + stack * (h + 2) - 2 : image.y + image.h - h - stack * (h + 2) + 2;
  x = MAX(0, MIN(x, win->frame.w - w));
  y = MAX(0, MIN(y, win->frame.h - h));
  irect16_t badge_rect = R(x, y, w, h);
  irect16_t text_rect = rect_trim_top(badge_rect, 2);
  fill_rect(get_sys_color(brTextNormal), rect_inset(badge_rect, -1));
  fill_rect(bg, badge_rect);
  draw_text_clipped("Caption", text, &text_rect, fg, TEXT_ALIGN_CENTER);
}

static void aim_paint(window_t *win, const agent_icon_state_t *st) {
  irect16_t local = get_client_rect(win);
  irect16_t image = aim_image_rect(win, st);
  int status_h = st->status_texture ? ICON_STATUS_SIZE : 0;
  int label_h = MAX(text_char_height("Caption"), status_h) + 2;
  int left_strip = st->input_count ? AGENT_ICON_STRIP_W : 0;
  int right_strip = st->output_count ? AGENT_ICON_STRIP_W : 0;
  irect16_t content = rect_trim_left(rect_trim_right(local, right_strip), left_strip);
  irect16_t label = rect_split_bottom(content, label_h); label.w = content.w;
  uint32_t bg = get_sys_color(brWorkspaceBg);
  if (!(win->flags & WINDOW_TRANSPARENT)) fill_rect(bg, local);
  if (win->value) {
    uint32_t sel = get_sys_color(brActiveTitlebar);
    irect16_t border = rect_inset(local, 1);
    irect16_t sides = rect_trim_bottom(rect_trim_top(border, 2), 2);
    fill_rect(sel, rect_split_top(border, 2));    fill_rect(sel, rect_split_bottom(border, 2));
    fill_rect(sel, rect_split_left(sides, 2));    fill_rect(sel, rect_split_right(sides, 2));
  }
  if (st->image_texture) draw_rect((int)st->image_texture, image);
  uint32_t text_col = win->value ? get_sys_color(brAccent) : get_sys_color(brTextNormal);
  if (st->status_texture) {
    int text_w = text_strwidth("Caption", win->title);
    int group_w = ICON_STATUS_SIZE + ICON_STATUS_GAP + text_w;
    irect16_t group_area = label; group_area.w = MAX(group_area.w, group_w);
    irect16_t group = rect_center(group_area, group_w, label.h);
    irect16_t status = rect_center(rect_split_left(group, ICON_STATUS_SIZE), ICON_STATUS_SIZE, ICON_STATUS_SIZE);
    irect16_t status_label = rect_trim_left(group, ICON_STATUS_SIZE + ICON_STATUS_GAP);
    draw_rect((int)st->status_texture, status);
    draw_text_clipped("Caption", win->title, &status_label, text_col, TEXT_ALIGN_CENTER);
  } else draw_text_clipped("Caption", win->title, &label, text_col, TEXT_ALIGN_CENTER);
  if (st->badge_text[0]) aim_draw_badge(win, image, st->badge_text, st->badge_bg, st->badge_fg, 4, 0);
  // Output artifacts (right)
  for (int i = 0; i < st->output_count; i++) {
    const agent_artifact_state_t *a = &st->output[i];
    irect16_t r = aim_output_rect(win, st, i);
    if (st->artifact_pending_col == 1 && st->artifact_pending_idx == i && st->artifact_dragging) continue;
    draw_rect((int)a->texture, r);
    if (a->count > 1) {
      int bs = AGENT_ICON_ARTIFACT_BADGE_SIZE;
      irect16_t badge = rect_offset(rect_split_bottom(rect_split_right(r, bs), bs), 2, 2);
      irect16_t cr = rect_offset(rect_trim_top(badge, 1), 1, 0);
      char cnt[2] = { a->count >= 10 ? '#' : (char)('0' + a->count), '\0' };
      draw_rect((int)a->count_badge_texture, badge);
      draw_text_clipped("Caption", cnt, &cr, 0xffffffff, TEXT_ALIGN_CENTER);
    }
  }
  // Input artifacts (left)
  for (int i = 0; i < st->input_count; i++) {
    const agent_artifact_state_t *a = &st->input[i];
    irect16_t r = aim_input_rect(win, st, i);
    if (st->artifact_pending_col == 0 && st->artifact_pending_idx == i && st->artifact_dragging) continue;
    draw_rect((int)a->texture, r);
    if (a->count > 1) {
      int bs = AGENT_ICON_ARTIFACT_BADGE_SIZE;
      irect16_t badge = rect_offset(rect_split_bottom(rect_split_left(r, bs), bs), -2, 2);
      irect16_t cr = rect_offset(rect_trim_top(badge, 1), 1, 0);
      char cnt[2] = { a->count >= 10 ? '#' : (char)('0' + a->count), '\0' };
      draw_rect((int)a->count_badge_texture, badge);
      draw_text_clipped("Caption", cnt, &cr, 0xffffffff, TEXT_ALIGN_CENTER);
    }
  }
}

static void aim_copy_artifact(agent_artifact_state_t *dst, const agent_artifact_t *src) {
  dst->id = src->id; dst->count = src->count;
  dst->texture = src->texture; dst->image_w = src->image_w; dst->image_h = src->image_h;
  dst->item_data = src->item_data;
  dst->count_badge_texture = src->count_badge_texture;
  dst->count_badge_w = src->count_badge_w; dst->count_badge_h = src->count_badge_h;
  if (src->label) { strncpy(dst->label, src->label, sizeof(dst->label) - 1); dst->label[sizeof(dst->label) - 1] = '\0'; }
  else dst->label[0] = '\0';
}

result_t win_agent_icon(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  agent_icon_state_t *st = (agent_icon_state_t *)win->userdata2;
  switch (msg) {
    case evCreate: {
      st = calloc(1, sizeof(*st));
      if (!st) return false;
      st->artifact_pending_col = -1;
      win->userdata2 = st;
      win->flags |= WINDOW_NOTITLE | WINDOW_NORESIZE;
      if (lparam) {
        icon_params_t *params = (icon_params_t *)lparam;
        st->image_texture = params->image.texture;
        st->image_w = params->image.width; st->image_h = params->image.height;
        st->item_data = params->item_data;
        st->draggable = params->draggable;
        st->notify_window = params->notify_window;
      }
      return true;
    }
    case evDestroy: aim_hide_ghost(st); free(st); win->userdata2 = NULL; return true;
    case evPaint: if (st && win->proc == win_agent_icon) aim_paint(win, st); return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) { m->desired_w = MAX(m->desired_w, 128); m->desired_h = MAX(m->desired_h, 128); }
      return true;
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) win->frame = a->rect;
      return true;
    }
    case evLeftButtonDown: {
      if (win->proc != win_agent_icon) return false;
      ipoint16_t point = aim_mouse_point(wparam);
      window_set_state(win, WINDOW_STATE_PRESSED, true);
      aim_select(win, true, true);
      if (st) {
        st->artifact_pending_col = -1; st->artifact_dragging = false;
        // Check output artifacts (right)
        for (int i = 0; i < st->output_count; i++) {
          irect16_t slot = aim_output_rect(win, st, i);
          if (rect_contains_point(slot, point)) {
            st->artifact_pending_col = 1; st->artifact_pending_idx = i;
            st->artifact_grab_x = point.x - slot.x; st->artifact_grab_y = point.y - slot.y;
            st->drag_x = point.x; st->drag_y = point.y; set_capture(win); invalidate_window(win); return true;
          }
        }
        // Check input artifacts (left)
        for (int i = 0; i < st->input_count; i++) {
          irect16_t slot = aim_input_rect(win, st, i);
          if (rect_contains_point(slot, point)) {
            st->artifact_pending_col = 0; st->artifact_pending_idx = i;
            st->artifact_grab_x = point.x - slot.x; st->artifact_grab_y = point.y - slot.y;
            st->drag_x = point.x; st->drag_y = point.y; set_capture(win); invalidate_window(win); return true;
          }
        }
      }
      if (st && st->draggable) {
        st->drag_pending = true; st->dragging = false;
        st->drag_x = point.x; st->drag_y = point.y;
        set_capture(win);
      }
      invalidate_window(win);
      return true;
    }
    case evMouseMove: {
      if (win->proc != win_agent_icon) return false;
      ipoint16_t point = aim_mouse_point(wparam);
      if (st && st->artifact_pending_col >= 0) {
        if (!st->artifact_dragging &&
            (abs(point.x - st->drag_x) >= AGENT_ICON_DRAG_THRESHOLD || abs(point.y - st->drag_y) >= AGENT_ICON_DRAG_THRESHOLD)) {
          st->artifact_dragging = true; invalidate_window(win);
        }
        if (st->artifact_dragging) aim_update_ghost(win, st, point);
        return true;
      }
      if (!st || !st->drag_pending) return false;
      if (!st->dragging && abs(point.x - st->drag_x) < AGENT_ICON_DRAG_THRESHOLD &&
                           abs(point.y - st->drag_y) < AGENT_ICON_DRAG_THRESHOLD) return true;
      st->dragging = true;
      int nx = win->frame.x + point.x - st->drag_x, ny = win->frame.y + point.y - st->drag_y;
      if (win->parent) {
        nx = MAX(0, MIN(nx, win->parent->frame.w - win->frame.w));
        ny = MAX(0, MIN(ny, win->parent->frame.h - win->frame.h));
      }
      move_window(win, nx, ny);
      return true;
    }
    case evLeftButtonUp: {
      if (win->proc != win_agent_icon) return false;
      if (st && st->artifact_pending_col >= 0) {
        ipoint16_t point = aim_mouse_point(wparam);
        window_t *target = st->artifact_dragging ? aim_drop_target(win, point) : NULL;
        bool accepted = false;
        aim_hide_ghost(st); set_capture(NULL);
        window_set_state(win, WINDOW_STATE_PRESSED, false);
        if (target) {
          agent_icon_state_t *target_st = target->userdata2;
          int target_cx = target->frame.x + target->frame.w / 2;
          bool input_side = point.x + win->frame.x < target_cx;
          agent_artifact_state_t *src_art = st->artifact_pending_col == 0
            ? &st->input[st->artifact_pending_idx] : &st->output[st->artifact_pending_idx];
          agent_artifact_drop_t drop = { win, target, src_art->id, src_art->item_data, input_side };
          window_t *notify_target = win->parent ? win->parent : get_root_window(win);
          accepted = send_message(notify_target, evCommand, MAKEDWORD(win->id, aimnArtifactDrop), &drop) != 0;
          (void)target_st;
        }
        st->artifact_pending_col = -1; st->artifact_dragging = false;
        if (!accepted) invalidate_window(win);
        return true;
      }
      bool dragged = st && st->drag_pending && st->dragging;
      if (st && st->drag_pending) {
        st->drag_pending = false; st->dragging = false;
        set_capture(NULL);
      }
      window_set_state(win, WINDOW_STATE_PRESSED, false);
      invalidate_window(win);
      if (!dragged) aim_notify(win, icnClicked);
      return true;
    }
    case evLeftButtonDoubleClick:
      if (win->proc != win_agent_icon) return false;
      aim_select(win, true, true);
      aim_notify(win, icnOpen);
      return true;
    case evKeyDown:
      if (win->proc != win_agent_icon) return false;
      if (wparam == AX_KEY_SPACE) { aim_select(win, true, true); aim_notify(win, icnClicked); return true; }
      if (wparam == AX_KEY_ENTER) { aim_select(win, true, true); aim_notify(win, icnOpen); return true; }
      return false;
    case aimSetInputArtifacts: {
      if (!st || wparam > AGENT_ICON_MAX_ARTIFACTS || (wparam && !lparam)) return false;
      aim_hide_ghost(st);
      memset(st->input, 0, sizeof(st->input)); st->input_count = (int)wparam;
      st->artifact_pending_col = -1; st->artifact_dragging = false;
      agent_artifact_t *src = (agent_artifact_t *)lparam;
      for (int i = 0; i < st->input_count; i++) aim_copy_artifact(&st->input[i], &src[i]);
      invalidate_window(win); return true;
    }
    case aimSetOutputArtifacts: {
      if (!st || wparam > AGENT_ICON_MAX_ARTIFACTS || (wparam && !lparam)) return false;
      aim_hide_ghost(st);
      memset(st->output, 0, sizeof(st->output)); st->output_count = (int)wparam;
      st->artifact_pending_col = -1; st->artifact_dragging = false;
      agent_artifact_t *src = (agent_artifact_t *)lparam;
      for (int i = 0; i < st->output_count; i++) aim_copy_artifact(&st->output[i], &src[i]);
      invalidate_window(win); return true;
    }
    case icSetImage: {
      if (!st || !lparam) return false;
      icon_image_t *img = (icon_image_t *)lparam;
      st->image_texture = img->texture; st->image_w = img->width; st->image_h = img->height;
      invalidate_window(win); return true;
    }
    case icSetStatusImage: {
      if (!st) return false;
      if (lparam) {
        icon_image_t *img = (icon_image_t *)lparam;
        st->status_texture = img->texture; st->status_w = img->width; st->status_h = img->height;
      } else { st->status_texture = 0; st->status_w = st->status_h = 0; }
      invalidate_window(win); return true;
    }
    case icSetBadge: {
      if (!st || !lparam) return false;
      icon_badge_t *src = (icon_badge_t *)lparam;
      if (src->text) { strncpy(st->badge_text, src->text, ICON_BADGE_TEXT_MAX); st->badge_text[ICON_BADGE_TEXT_MAX] = '\0'; }
      else st->badge_text[0] = '\0';
      st->badge_bg = src->background; st->badge_fg = src->foreground; st->badge_anchor = src->anchor;
      invalidate_window(win); return true;
    }
    case icClearBadges:
      if (!st) return false;
      st->badge_text[0] = '\0';
      invalidate_window(win); return true;
    case icSetSelected: aim_select(win, wparam != 0, false); return true;
    case icGetSelected: return win->value != 0;
    case icSetItemData: if (st) st->item_data = lparam; return st != NULL;
    case icGetItemData: return st ? (result_t)st->item_data : 0;
    default: return false;
  }
}
