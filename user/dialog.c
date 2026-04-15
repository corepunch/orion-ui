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

// Application running flag – defined here as the authoritative definition;
// all other modules declare it as `extern bool running`.
bool running;

// Shared modal message loop.  Runs until the dialog window is destroyed or the
// application exits.  Forces a full repaint after the dialog is gone.
static uint32_t run_dialog_loop(window_t *dlg, window_t *parent) {
  // Guard against the window being destroyed during kWindowMessageCreate
  // (e.g. end_dialog called from the window proc's create handler).
  if (!is_window(dlg)) return 0;
  uint32_t result = 0;
  ui_event_t event;
  dlg->userdata2 = &result;
  if (parent) enable_window(parent, false);
  show_window(dlg, true);
  while (running && is_window(dlg)) {
    while (get_message(&event)) {
      dispatch_message(&event);
    }
    repost_messages();
  }
  if (parent) enable_window(parent, true);
  // Force a clean stencil rebuild + full repaint to clear the area the dialog
  // occupied.  destroy_window's recursive child teardown re-queues
  // kWindowMessageRefreshStencil *after* any already-pending paint messages
  // (a dedup side-effect), so the repaint inside the loop above may paint
  // against a stale stencil, leaving a ghost.  Posting a fresh RefreshStencil
  // here, after all dialog windows are gone, guarantees correct ordering.
  if (running) {
    post_message((window_t*)1, kWindowMessageRefreshStencil, 0, NULL);
    for (window_t *w = windows; w; w = w->next) {
      if (w->visible) invalidate_window(w);
    }
    repost_messages();
  }
  return result;
}

uint32_t show_dialog(char const *title,
                     const rect_t* frame,
                     window_t *parent,
                     winproc_t proc,
                     void *param)
{
  uint32_t flags = WINDOW_VSCROLL|WINDOW_DIALOG|WINDOW_NOTRAYBUTTON;
  const char *dialog_title = title ? title : "";
  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  window_t *dlg = create_window(dialog_title, flags, frame, NULL, proc, hinstance, param);
  return run_dialog_loop(dlg, parent);
}

// Show a modal dialog whose layout is described by a form_def_t.
// Children are instantiated (via create_window_from_form) before
// kWindowMessageCreate fires, so the window proc can find them already in
// place — analogous to WinAPI DialogBoxIndirectParam.
// title overrides def->name when non-NULL.  The dialog is centered on screen.
uint32_t show_dialog_from_form(form_def_t const *def, char const *title,
                               window_t *parent, winproc_t proc, void *param)
{
  if (!def || !proc) return 0;

  // Merge dialog-specific flags into a local copy of the definition.
  form_def_t dlg_def = *def;
  dlg_def.flags |= WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON;
  if (title) dlg_def.name = title;

  // Center on screen.
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  int x = (sw - def->w) / 2;
  int y = (sh - def->h) / 2;

  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  window_t *dlg = create_window_from_form(&dlg_def, x, y, NULL, proc, hinstance, param);
  if (!dlg) return 0;
  return run_dialog_loop(dlg, parent);
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
      case BIND_INT_COMBO: {
        window_t *ctrl = get_window_item(win, b[i].ctrl_id);
        if (ctrl) {
          int v = *(const int *)(base + b[i].offset);
          if (v < 0) v = (int)b[i].size;
          send_message(ctrl, kComboBoxMessageSetCurrentSelection,
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
      case BIND_INT_COMBO: {
        int v = (int)send_message(ctrl, kComboBoxMessageGetCurrentSelection, 0, NULL);
        *(int *)(base + b[i].offset) = (v >= 0) ? v : (int)b[i].size;
        break;
      }
      case BIND_INT_EDIT:
        *(int *)(base + b[i].offset) = atoi(ctrl->title);
        break;
    }
  }
}
