// Modal dialog implementation (WinAPI-style show_dialog / end_dialog).
//
// show_dialog() creates a top-level WINDOW_DIALOG window, disables the
// parent, then runs a nested message loop until end_dialog() (or the X
// button) destroys the dialog.  The return value is the code passed to
// end_dialog(), exactly like Win32 DialogBoxParam / EndDialog.
//
// Reentrancy: each show_dialog() call keeps its result in a stack-local
// dlg_ctx_t whose address is stored in the dialog window's userdata2 field.
// end_dialog() writes through that pointer before destroying the window, so
// nested dialogs each see only their own result code.

#include <SDL2/SDL.h>
#include "gl_compat.h"

#include "../ui.h"

// Application running flag – defined here as the authoritative definition;
// all other modules declare it as `extern bool running`.
bool running;

// Per-dialog context (stack-allocated inside show_dialog).
typedef struct {
  uint32_t result;
} dlg_ctx_t;

uint32_t show_dialog(char const *title,
                     const rect_t* frame,
                     window_t *parent,
                     winproc_t proc,
                     void *param)
{
  SDL_Event event;
  uint32_t flags = WINDOW_VSCROLL|WINDOW_DIALOG|WINDOW_NOTRAYBUTTON;
  dlg_ctx_t ctx = {0};
  window_t *dlg = create_window(title, flags, frame, NULL, proc, param);
  dlg->userdata2 = &ctx;
  if (parent) enable_window(parent, false);
  show_window(dlg, true);
  while (running && is_window(dlg)) {
    while (get_message(&event)) {
      dispatch_message(&event);
    }
    repost_messages();
  }
  if (parent) enable_window(parent, true);
  return ctx.result;
}

// Store the result code and destroy the dialog.
// win may be any window inside the dialog (e.g. a button); get_root_window
// walks up to the dialog window that owns the dlg_ctx_t.
void end_dialog(window_t *win, uint32_t code) {
  window_t *root = get_root_window(win);
  if (root->userdata2) {
    dlg_ctx_t *ctx = (dlg_ctx_t *)root->userdata2;
    ctx->result = code;
  }
  destroy_window(root);
}
