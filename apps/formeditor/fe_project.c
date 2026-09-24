// Project-level document management for Form Editor.

#include "formeditor.h"

// ============================================================
// Document title
// ============================================================

void form_doc_update_title(window_t *doc) {
  form_doc_state_t *st = fe_doc_state(doc);
  if (!doc || !st) return;
  char current[sizeof(doc->title)];
  snprintf(current, sizeof(current), "%s", doc->title[0] ? doc->title : "Untitled");
  size_t len = strlen(current);
  if (len >= 2 && strcmp(current + len - 2, " *") == 0) current[len - 2] = '\0';
  const char *name = current;
  const char *slash = strrchr(name, '/');
  if (slash) name = slash + 1;
  snprintf(doc->title, sizeof(doc->title), "%s%s",
           name, st->modified ? " *" : "");
  invalidate_window(doc);
}

void form_doc_activate(window_t *doc) {
  if (!g_app || !doc) return;
  if (g_app->active_form == doc) return;
  window_t *prev = g_app->active_form;
  g_app->active_form = doc;
  if (prev)
    invalidate_window(prev);
  invalidate_window(doc);
  fe_notify(FE_EVENT_DOCUMENT_ACTIVATED, doc);
}

void form_doc_show_only(window_t *doc) {
  if (!g_app || !doc) return;
  for (int i = 0; i < g_app->form_count; i++) {
    window_t *w = g_app->forms[i];
    if (w && w != doc && is_window(w))
      show_window(w, false);
  }
  form_doc_activate(doc);
  if (is_window(doc))
    show_window(doc, true);
}

// ============================================================
// create_form_doc / close_form_doc
// ============================================================

irect16_t form_doc_frame_for_size(int form_w, int form_h, uint32_t form_flags) {
  int max_w = SCREEN_W - 4;
  int max_h = SCREEN_H - get_theme()->menubar_height - 4;
  bool has_status = (form_flags & WINDOW_STATUSBAR) != 0;
  bool has_toolbar = (form_flags & WINDOW_TOOLBAR) != 0;
  int status_h = has_status ? STATUSBAR_HEIGHT : 0;
  int toolbar_h = has_toolbar ? theme_toolbar_band_height() : 0;
  bool needs_hscroll = form_w > max_w;
  int hstrip = (needs_hscroll && !has_status) ? get_theme()->scrollbar_width : 0;
  int max_canvas_h = max_h - get_theme()->caption_height - toolbar_h - status_h - hstrip;
  bool needs_vscroll;
  int frame_w;
  int frame_h;

  if (max_w < 1) max_w = 1;
  if (max_canvas_h < 1) max_canvas_h = 1;

  needs_vscroll = form_h > max_canvas_h;
  frame_w = form_w + (needs_vscroll ? get_theme()->scrollbar_width : 0);
  if (frame_w > max_w) frame_w = max_w;

  frame_h = get_theme()->caption_height + toolbar_h + status_h + hstrip + form_h;
  if (frame_h > max_h) frame_h = max_h;

  return (irect16_t){CW_USEDEFAULT, CW_USEDEFAULT, frame_w, frame_h};
}



window_t *create_form_doc(int w, int h) {
  if (!g_app) return NULL;
  if (w <= 0 || h <= 0 || w > INT16_MAX || h > INT16_MAX) return NULL;
  window_t *prev_doc = g_app->active_form;
  if (g_app->form_count >= MAX_ELEMENTS)
    return NULL;

  form_doc_state_t *doc = (form_doc_state_t *)calloc(1, sizeof(form_doc_state_t));
  if (!doc)
    return NULL;

  doc->modified  = false;

  // Document window
  uint32_t doc_flags = fe_default_auto_layout_enabled() ? WINDOW_AUTO_LAYOUT : 0;
  doc_flags &= ~WINDOW_STACK_HORIZONTAL;
  irect16_t doc_frame = form_doc_frame_for_size(w, h, doc_flags);
  set_default_window_position(DOC_START_X, DOC_START_Y);
  window_t *dwin = create_window(
      "Untitled",
      WINDOW_HSCROLL | (doc_flags & (WINDOW_TOOLBAR | WINDOW_STATUSBAR)),
      &doc_frame,
      NULL, win_canvas_proc, g_app->hinstance, NULL);
  if (!dwin) {
    free(doc);
    return NULL;
  }
  dwin->userdata = doc;
  dwin->flags = (dwin->flags & ~(WINDOW_AUTO_LAYOUT | WINDOW_STACK_HORIZONTAL)) |
                (doc_flags & (WINDOW_AUTO_LAYOUT | WINDOW_STACK_HORIZONTAL));
  dwin->layout.layout_spacing = 4;
  dwin->layout.layout_padding = (irect16_t){0, 0, 0, 0};
  dwin->layout.layout_margin = (irect16_t){0, 0, 0, 0};

  // One-window mode: attach the runtime preview directly under the document.
  canvas_rebuild_live_controls(dwin);

  g_app->forms[g_app->form_count++] = dwin;
  g_app->active_form = dwin;

  show_window(dwin, true);
  if (prev_doc)
    invalidate_window(prev_doc);
  form_doc_update_title(dwin);
  send_message(dwin, evStatusBar, 0, (void *)"New form");
  fe_notify(FE_EVENT_DOCUMENT_CREATED, dwin);
  return dwin;
}

void close_form_doc(window_t *doc) {
  if (!doc) return;
  form_doc_state_t *st = fe_doc_state(doc);
  if (g_app) {
    window_t *doc_win = doc;
    int found = -1;
    for (int i = 0; i < g_app->form_count; i++) {
      if (g_app->forms[i] == doc_win) {
        found = i;
        break;
      }
    }
    if (found >= 0) {
      for (int i = found; i + 1 < g_app->form_count; i++)
        g_app->forms[i] = g_app->forms[i + 1];
      g_app->forms[g_app->form_count - 1] = NULL;
      g_app->form_count--;
    }
    if (g_app->active_form == doc)
      g_app->active_form = (g_app->form_count > 0 && g_app->forms[0]) ? g_app->forms[0] : NULL;
  }
  if (is_window(doc)) {
    fe_project_clear_doc_xml(doc);
    doc->userdata = NULL;
    destroy_window(doc);
  }
  fe_notify(FE_EVENT_DOCUMENT_CLOSED, NULL);
  free(st);
}
