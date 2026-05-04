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
  irect16_t dlg_frame = {0, 0, width, height};
  dlg_frame = center_window_rect(dlg_frame, parent);
  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  window_t *dlg = create_window_proc(dialog_title, flags, &dlg_frame, NULL, proc, hinstance, param);
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

  irect16_t wr = {0, 0, def->width, def->height};
  adjust_window_rect(&wr, dlg_def.flags);
  dlg_def.width = wr.w;
  dlg_def.height = wr.h;

  irect16_t dlg_rect = center_window_rect((irect16_t){0, 0, wr.w, wr.h}, parent);

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

// ── Generic DDX dialog proc ───────────────────────────────────────────────
// Used by show_ddx_dialog().  The lparam passed to evCreate is a pointer to a
// ddx_dlg_ctx_t that lives on show_ddx_dialog()'s stack for the lifetime of
// the modal loop (safe because show_dialog_from_form_ex blocks until close).

typedef struct {
  form_def_t const *def;   // form definition carrying bindings + ok/cancel IDs
  void             *state; // caller-supplied state for push/pull
} ddx_dlg_ctx_t;

static result_t dialog_ddx_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  ddx_dlg_ctx_t *ctx = (ddx_dlg_ctx_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      ctx = (ddx_dlg_ctx_t *)lparam;
      win->userdata = ctx;
      if (ctx->def->bindings && ctx->def->binding_count > 0)
        dialog_push(win, ctx->state,
                    ctx->def->bindings, ctx->def->binding_count);
      return true;
    }

    case evCommand: {
      if (!ctx) return false;
      uint16_t notif = HIWORD(wparam);

      // Pressing Enter (or Tab) in a single-line edit box fires edUpdate.
      // Treat this as equivalent to clicking the OK button.
      if (notif == edUpdate) {
        if (ctx->def->bindings)
          dialog_pull(win, ctx->state,
                      ctx->def->bindings, ctx->def->binding_count);
        end_dialog(win, 1);
        return true;
      }

      if (notif != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (ctx->def->ok_id && src->id == ctx->def->ok_id) {
        if (ctx->def->bindings)
          dialog_pull(win, ctx->state,
                      ctx->def->bindings, ctx->def->binding_count);
        end_dialog(win, 1);
        return true;
      }
      if (ctx->def->cancel_id && src->id == ctx->def->cancel_id) {
        end_dialog(win, 0);
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

uint32_t show_ddx_dialog(form_def_t const *def, const char *title,
                         window_t *parent, void *state) {
  if (!def) return 0;
  ddx_dlg_ctx_t ctx = { .def = def, .state = state };
  return show_dialog_from_form_ex(def, title, parent,
                                  WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                  dialog_ddx_proc, &ctx);
}
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

void ddx_push_int(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  int v = *(const int *)(base + b->offset);
  set_window_item_text(dlg, b->ctrl_id, "%d", v);
}

void ddx_pull_int(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = atoi(ctrl->title);
  *(int *)(base + b->offset) = v;
}

void ddx_push_float(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  float v = *(const float *)(base + b->offset);
  set_window_item_text(dlg, b->ctrl_id, "%.2f", v);
}

void ddx_pull_float(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  float v;
  if (!ctrl) return;
  v = strtof(ctrl->title, NULL);
  *(float *)(base + b->offset) = v;
}

void ddx_push_u8(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  uint8_t v = *(const uint8_t *)(base + b->offset);
  set_window_item_text(dlg, b->ctrl_id, "%u", (unsigned)v);
}

void ddx_pull_u8(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = atoi(ctrl->title);
  *(uint8_t *)(base + b->offset) = (uint8_t)CLAMP(v, 0, 255);
}

void ddx_push_text(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (!ctrl) return;
  send_message(ctrl, edSetText, 0, (void *)(base + b->offset));
}

void ddx_pull_text(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (!ctrl || b->wparam == 0) return;
  send_message(ctrl, edGetText, (uint32_t)b->wparam, base + b->offset);
}

void ddx_push_combo(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = *(const int *)(base + b->offset);
  if (v < 0) v = (int)b->wparam;
  send_message(ctrl, cbSetCurrentSelection, (uint32_t)v, NULL);
}

void ddx_pull_combo(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v = kComboBoxError;
  if (!ctrl) return;
  (void)send_message(ctrl, cbGetCurrentSelection, 0, &v);
  *(int *)(base + b->offset) = (v >= 0) ? v : (int)b->wparam;
}

void ddx_push_check(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  bool v;
  if (!ctrl) return;
  v = *(const bool *)(base + b->offset);
  send_message(ctrl, btnSetCheck, v ? btnStateChecked : btnStateUnchecked, NULL);
}

void ddx_pull_check(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = (int)send_message(ctrl, btnGetCheck, 0, NULL);
  *(bool *)(base + b->offset) = (v == btnStateChecked);
}

void dialog_push(window_t *win, const void *state,
                 const ctrl_binding_t *b, int n) {
  if (!win || !state || !b) return;
  const char *base = (const char *)state;
  for (int i = 0; i < n; i++) {
    if (b[i].push) {
      b[i].push(win, &b[i], state);
      continue;
    }

    window_t *ctrl = get_window_item(win, b[i].ctrl_id);
    if (!ctrl) continue;
    switch (b[i].getter) {
      case 0:
        break;
      case cbGetCurrentSelection: {
        int v = *(const int *)(base + b[i].offset);
        if (v < 0) v = (int)b[i].wparam;
        send_message(ctrl, cbSetCurrentSelection, (uint32_t)v, NULL);
        break;
      }
      case edGetText:
        send_message(ctrl, edSetText, 0, (void *)(base + b[i].offset));
        break;
      default:
        break;
    }
  }
}

int dialog_pull_command(window_t *win, void *state,
                        const ctrl_binding_t *b, int n,
                        uint16_t command) {
  if (!win || !state || !b) return 0;
  char *base = (char *)state;
  int applied = 0;
  for (int i = 0; i < n; i++) {
    if (command && b[i].command && b[i].command != command)
      continue;

    if (b[i].pull) {
      b[i].pull(win, &b[i], state);
      applied++;
      continue;
    }

    window_t *ctrl = get_window_item(win, b[i].ctrl_id);
    if (!ctrl) continue;
    switch (b[i].getter) {
      case 0:
        applied++;
        break;
      case cbGetCurrentSelection: {
        int v = kComboBoxError;
        (void)send_message(ctrl, cbGetCurrentSelection, 0, &v);
        *(int *)(base + b[i].offset) = (v >= 0) ? v : (int)b[i].wparam;
        applied++;
        break;
      }
      case edGetText: {
        char  *dst = base + b[i].offset;
        size_t sz  = b[i].wparam;
        if (sz > 0)
          send_message(ctrl, edGetText, (uint32_t)sz, dst);
        applied++;
        break;
      }
      default:
        (void)send_message(ctrl, b[i].getter, (uint32_t)b[i].wparam,
                           base + b[i].offset);
        applied++;
        break;
    }
  }
  return applied;
}

void dialog_pull(window_t *win, void *state,
                 const ctrl_binding_t *b, int n) {
  (void)dialog_pull_command(win, state, b, n, 0);
}
