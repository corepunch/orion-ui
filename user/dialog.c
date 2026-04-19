// Modal dialog implementation (WinAPI-style show_dialog / end_dialog).
//
// show_dialog() creates a top-level WINDOW_DIALOG window, disables the
// parent, then runs a nested message loop until end_dialog() (or the X
// button) destroys the dialog.  The return value is the code passed to
// end_dialog(), exactly like Win32 DialogBoxParam / EndDialog.
//
// Reentrancy: each show_dialog() call keeps its result in a stack-local
// uint32_t whose address is stored in the dialog window's userdata2 field.
// end_dialog() writes through that pointer before destroying the window, so
// nested dialogs each see only their own result code.

#include <stdlib.h>
#include <string.h>
#include "../ui.h"

static rect_t center_modal_rect(rect_t rect, window_t *parent) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  window_t *owner = parent ? get_root_window(parent) : NULL;

  if (owner) {
    rect.x = owner->frame.x + (owner->frame.w - rect.w) / 2;
    rect.y = owner->frame.y + (owner->frame.h - rect.h) / 2;
  } else {
    rect.x = (sw - rect.w) / 2;
    rect.y = (sh - rect.h) / 2;
  }

  if (rect.w >= sw)
    rect.x = 0;
  else
    rect.x = MAX(0, MIN(rect.x, sw - rect.w));

  if (rect.h >= sh)
    rect.y = 0;
  else
    rect.y = MAX(0, MIN(rect.y, sh - rect.h));

  return rect;
}

// Shared modal message loop.  Runs until the dialog window is destroyed or the
// application exits.  Forces a full repaint after the dialog is gone.
static uint32_t run_dialog_loop(window_t *dlg, window_t *parent) {
  // Guard against the window being destroyed during evCreate
  // (e.g. end_dialog called from the window proc's create handler).
  if (!is_window(dlg)) return 0;
  uint32_t result = 0;
  ui_event_t event;
  window_t *modal_parent = parent ? get_root_window(parent) : NULL;
  window_t *prev_modal_overlay_parent = g_ui_runtime.modal_overlay_parent;
  dlg->userdata2 = &result;
  if (modal_parent) {
    g_ui_runtime.modal_overlay_parent = modal_parent;
    enable_window(modal_parent, false);
  }
  show_window(dlg, true);
  while (g_ui_runtime.running && is_window(dlg)) {
    while (get_message(&event)) {
      dispatch_message(&event);
    }
    repost_messages();
  }
  if (modal_parent) {
    enable_window(modal_parent, true);
    set_focus(modal_parent);
    g_ui_runtime.modal_overlay_parent = prev_modal_overlay_parent;
  }
  // Force a clean stencil rebuild + full repaint to clear the area the dialog
  // occupied.  destroy_window's recursive child teardown re-queues
  // evRefreshStencil *after* any already-pending paint messages
  // (a dedup side-effect), so the repaint inside the loop above may paint
  // against a stale stencil, leaving a ghost.  Posting a fresh RefreshStencil
  // here, after all dialog windows are gone, guarantees correct ordering.
  if (g_ui_runtime.running) {
    post_message((window_t*)1, evRefreshStencil, 0, NULL);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (w->visible) invalidate_window(w);
    }
    repost_messages();
  }
  return result;
}

uint32_t show_dialog_ex(char const *title,
                        int width,
                        int height,
                        window_t *parent,
                        uint32_t flags,
                        winproc_t proc,
                        void *param)
{
  const char *dialog_title = title ? title : "";
  rect_t dlg_frame = {0, 0, width, height};
  dlg_frame = center_modal_rect(dlg_frame, parent);
  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  window_t *dlg = create_window(dialog_title, flags, &dlg_frame, NULL, proc, hinstance, param);
  return run_dialog_loop(dlg, parent);
}

uint32_t show_dialog(char const *title,
                     int width,
                     int height,
                     window_t *parent,
                     winproc_t proc,
                     void *param)
{
  return show_dialog_ex(title, width, height, parent,
                        WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                        proc, param);
}

// Show a modal dialog whose layout is described by a form_def_t.
// Children are instantiated (via create_window_from_form) before
// evCreate fires, so the window proc can find them already in
// place — analogous to WinAPI DialogBoxIndirectParam.
// title overrides def->name when non-NULL.  The dialog is centered on owner.
uint32_t show_dialog_from_form_ex(form_def_t const *def, char const *title,
                                  window_t *parent, uint32_t flags,
                                  winproc_t proc, void *param)
{
  if (!def || !proc) return 0;

  // Merge dialog-specific flags into a local copy of the definition.
  form_def_t dlg_def = *def;
  dlg_def.flags |= flags;
  if (title) dlg_def.name = title;

  rect_t wr = {0, 0, def->width, def->height};
  adjust_window_rect(&wr, dlg_def.flags);
  dlg_def.width = wr.w;
  dlg_def.height = wr.h;

  rect_t dlg_rect = center_modal_rect((rect_t){0, 0, wr.w, wr.h}, parent);

  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  window_t *dlg = create_window_from_form(&dlg_def, dlg_rect.x, dlg_rect.y,
                                          NULL, proc, hinstance, param);
  if (!dlg) return 0;
  return run_dialog_loop(dlg, parent);
}

uint32_t show_dialog_from_form(form_def_t const *def, char const *title,
                               window_t *parent, winproc_t proc, void *param)
{
  return show_dialog_from_form_ex(def, title, parent,
                                  WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                  proc, param);
}

// Store the result code and destroy the dialog.
// win may be any window inside the dialog (e.g. a button); get_root_window
// walks up to the dialog window that owns the result pointer.
void end_dialog(window_t *win, uint32_t code) {
  window_t *root = get_root_window(win);
  if (root->userdata2) {
    *(uint32_t *)root->userdata2 = code;
  }
  destroy_window(root);
}

// ── Dialog Data Exchange (DDX) ────────────────────────────────────────────
// Push state → controls (populate on open); pull controls → state (read on OK).

void dialog_push(window_t *win, const void *state,
                 const ctrl_binding_t *b, int n) {
  if (!win || !state || !b) return;
  const char *base = (const char *)state;
  for (int i = 0; i < n; i++) {
    switch (b[i].type) {
      case BIND_STRING: {
        int max_len = b[i].size > 0 ? b[i].size - 1 : 0;
        set_window_item_text(win, b[i].ctrl_id, "%.*s",
                             max_len, base + b[i].offset);
        break;
      }
      case BIND_MLSTRING: {
        window_t *ctrl = get_window_item(win, b[i].ctrl_id);
        if (ctrl)
          send_message(ctrl, edSetText, 0,
                       (void *)(base + b[i].offset));
        break;
      }
      case BIND_INT_COMBO: {
        window_t *ctrl = get_window_item(win, b[i].ctrl_id);
        if (ctrl) {
          int v = *(const int *)(base + b[i].offset);
          if (v < 0) v = (int)b[i].size;
          send_message(ctrl, cbSetCurrentSelection,
                       (uint32_t)v, NULL);
        }
        break;
      }
      case BIND_INT_EDIT: {
        int v = *(const int *)(base + b[i].offset);
        set_window_item_text(win, b[i].ctrl_id, "%d", v);
        break;
      }
    }
  }
}

void dialog_pull(window_t *win, void *state,
                 const ctrl_binding_t *b, int n) {
  if (!win || !state || !b) return;
  char *base = (char *)state;
  for (int i = 0; i < n; i++) {
    window_t *ctrl = get_window_item(win, b[i].ctrl_id);
    if (!ctrl) continue;
    switch (b[i].type) {
      case BIND_STRING: {
        char  *dst = base + b[i].offset;
        size_t sz  = b[i].size;
        if (sz > 0) {
          strncpy(dst, ctrl->title, sz - 1);
          dst[sz - 1] = '\0';
        }
        break;
      }
      case BIND_MLSTRING: {
        char  *dst = base + b[i].offset;
        size_t sz  = b[i].size;
        if (sz > 0)
          send_message(ctrl, edGetText, (uint32_t)sz, dst);
        break;
      }
      case BIND_INT_COMBO: {
        int v = (int)send_message(ctrl, cbGetCurrentSelection, 0, NULL);
        *(int *)(base + b[i].offset) = (v >= 0) ? v : (int)b[i].size;
        break;
      }
      case BIND_INT_EDIT:
        *(int *)(base + b[i].offset) = atoi(ctrl->title);
        break;
    }
  }
}
