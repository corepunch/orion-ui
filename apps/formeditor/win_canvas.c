// Window-first FormEditor canvas. The live control tree is the edit model;
// XML on doc->userdata2 is the persistence model.

#include "formeditor.h"

static void canvas_restore_local_draw_space(window_t *win) {
  window_t *root = get_root_window(win);
  int t = titlebar_height(root);
  set_viewport_for_fbo(root);
  set_projection(root->hscroll.pos, -t + root->vscroll.pos,
                 root->frame.w + root->hscroll.pos,
                 root->frame.h - t + root->vscroll.pos);
  set_scissor_fbo(root, rect_offset(get_client_rect(root), 0, t));
}

static irect16_t client_rect_in_host(window_t *host, window_t *target) {
  if (!host || !target) return (irect16_t){0};
  irect16_t tr = get_client_rect(target);
  if (host == target) return (irect16_t){0, 0, tr.w, tr.h};
  ipoint16_t ho = {(int16_t)window_screen_x(host),
                   (int16_t)(window_screen_y(host) + titlebar_height(host))};
  ipoint16_t to = {(int16_t)window_screen_x(target),
                   (int16_t)(window_screen_y(target) + titlebar_height(target))};
  return (irect16_t){(int16_t)(to.x - ho.x), (int16_t)(to.y - ho.y), tr.w, tr.h};
}

static bool screen_point_in_window(window_t *win, int sx, int sy) {
  if (!win || !window_has_state(win, WINDOW_STATE_VISIBLE)) return false;
  irect16_t cr = get_client_rect(win);
  int x = window_screen_x(win), y = window_screen_y(win);
  return sx >= x && sy >= y && sx < x + cr.w && sy < y + cr.h;
}

static window_t *canvas_hit_screen_rec(window_t *win, int sx, int sy) {
  if (!screen_point_in_window(win, sx, sy)) return NULL;
  for (window_t *child = win->children; child; child = child->next) {
    window_t *hit = canvas_hit_screen_rec(child, sx, sy);
    if (hit) return hit;
  }
  return win;
}

static void canvas_select_runtime_window(window_t *doc, window_t *target) {
  form_doc_state_t *st = fe_doc_state(doc);
  if (!st) return;
  if (!target || get_root_window(target) != doc) target = doc->children ? doc->children : doc;
  if (st->selected_window == target) return;
  st->selected_window = target;
  invalidate_window(doc);
  property_browser_refresh(doc);
  fe_notify(FE_EVENT_SELECTION_CHANGED, doc);
}

lresult_t win_canvas_runtime_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

void canvas_rebuild_live_controls(window_t *doc) {
  form_doc_state_t *st = fe_doc_state(doc);
  if (!doc) return;
  if (st) {
    st->selected_window = NULL;
    st->drag_overlay_active = false;
  }
  while (doc->children) destroy_window(doc->children);
  fe_create_runtime_form_window(doc, doc, win_canvas_runtime_proc);
  if (st) st->selected_window = doc->children;
  invalidate_window(doc);
}

void canvas_set_component_drag_hover(window_t *doc, bool active, window_t *target) {
  form_doc_state_t *st = fe_doc_state(doc);
  if (!st) return;
  st->drag_overlay_active = active && target;
  st->drag_overlay_rect = st->drag_overlay_active
      ? client_rect_in_host(doc, target) : (irect16_t){0};
  invalidate_window(doc);
}

window_t *canvas_find_component_drop_target(window_t *doc, int type, int sx, int sy) {
  const fe_component_desc_t *desc = fe_component_at(type);
  if (!doc || !desc) return NULL;
  for (window_t *target = canvas_hit_screen_rec(doc, sx, sy);
       target && target != doc; target = target->parent) {
    if (!fe_component_rejects_parent(desc, target)) return target;
  }
  return doc->children && !fe_component_rejects_parent(desc, doc->children)
      ? doc->children : NULL;
}

bool canvas_drop_component_to_target(window_t *doc, int type, window_t *target,
                                     int screen_x, int screen_y) {
  if (!doc) return false;
  if (!target) target = canvas_find_component_drop_target(doc, type, screen_x, screen_y);
  if (!target || !fe_doc_drop_create_component(type, target)) return false;
  property_browser_refresh(doc);
  forms_browser_refresh();
  return true;
}

static bool canvas_table_column_at(window_t *doc, int sx, int sy,
                                   window_t **table_out, int *column_out) {
  window_t *hit = canvas_hit_screen_rec(doc, sx, sy);
  while (hit && hit != doc && !window_is_class(hit, "TableView")) hit = hit->parent;
  if (!hit || hit == doc || !window_is_class(hit, "TableView")) return false;
  int count = (int)send_message(hit, RVM_GETCOLUMNCOUNT, 0, NULL);
  irect16_t cr = get_client_rect(hit);
  int x = sx - window_screen_x(hit) + (int)hit->hscroll.pos, left = 0;
  for (int column = 0; column < count; column++) {
    int width = (int)send_message(hit, RVM_GETREPORTCOLUMNWIDTH, (uint32_t)column, NULL);
    if (width <= 0) width = count > 0 ? cr.w / count : cr.w;
    if (x >= left && x < left + width) {
      if (table_out) *table_out = hit;
      if (column_out) *column_out = column;
      return true;
    }
    left += width;
  }
  return false;
}

bool canvas_bind_database_field(window_t *doc, const fe_database_field_ref_t *field,
                                int screen_x, int screen_y) {
  window_t *table = NULL;
  int column = -1;
  if (!doc || !field || !canvas_table_column_at(doc, screen_x, screen_y, &table, &column))
    return false;
  xmlNodePtr table_node = fe_project_table_node_for_window(doc, table);
  char expression[128], title[128], error[256] = {0};
  if (!table_node || !fe_resolve_table_column_database_field(
      table_node, field, expression, sizeof(expression), title, sizeof(title),
      error, sizeof(error))) {
    message_box(doc, error[0] ? error : "Drop database fields onto a TableView column.",
                "Database Field", MB_OK);
    return false;
  }
  if (!fe_project_update_table_column_binding(doc, table, column, expression, title))
    return false;
  fe_doc_mark_modified(doc);
  if (g_app) g_app->project.modified = true;
  canvas_rebuild_live_controls(doc);
  property_browser_refresh(doc);
  forms_browser_refresh();
  return true;
}

lresult_t win_canvas_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  form_doc_state_t *st = fe_doc_state(win);
  switch (msg) {
    case evHitTest: return (lresult_t)(intptr_t)win;
    case evPaint:
      (void)lparam;
      for (window_t *child = win->children; child; child = child->next)
        send_message(child, evPaint, wparam, lparam);
      if (st && st->selected_window && st->selected_window != win) {
        canvas_restore_local_draw_space(win);
        draw_sel_rect(client_rect_in_host(win, st->selected_window));
      }
      if (st && st->drag_overlay_active) {
        canvas_restore_local_draw_space(win);
        draw_sel_rect(st->drag_overlay_rect);
      }
      return true;
    case evLeftButtonDown: {
      int sx = window_screen_x(win) + (int16_t)LOWORD(wparam) - win->hscroll.pos;
      int sy = window_screen_y(win) + (int16_t)HIWORD(wparam) - win->vscroll.pos;
      canvas_select_runtime_window(win, canvas_hit_screen_rec(win, sx, sy));
      return true;
    }
    case evSetFocus:
      if (st && window_has_state(win, WINDOW_STATE_VISIBLE)) form_doc_activate(win);
      return false;
    case evResize:
      window_layout_sync(win);
      invalidate_window(win);
      fe_doc_mark_modified(win);
      if (g_app) g_app->project.modified = true;
      return false;
    case evClose:
      if (!st) return false;
      show_window(win, false);
      forms_browser_refresh();
      return true;
    default: return false;
  }
}
