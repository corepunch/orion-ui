// Create tag dialog — uses generated form from gitclient.orion.

#include "gitclient.h"

typedef struct {
  char name[256];
  char ref[256];
} create_tag_state_t;

static const ctrl_binding_t tag_bindings[] = {
  DDX_TEXT(ID_CREATE_TAG_DIALOG_NAME, create_tag_state_t, name),
  DDX_TEXT(ID_CREATE_TAG_DIALOG_REF,  create_tag_state_t, ref),
};

static result_t create_tag_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  create_tag_state_t *st = (create_tag_state_t *)win->userdata;

  if (msg == evCreate) {
    win->userdata = lparam; st = (create_tag_state_t *)lparam;
    dialog_push(win, st, tag_bindings, ARRAY_LEN(tag_bindings));
    return true;
  }
  if (msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_CREATE_TAG_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
  if (id == ID_CREATE_TAG_DIALOG_OK) {
    dialog_pull(win, st, tag_bindings, ARRAY_LEN(tag_bindings));
    if (!st->name[0]) {
      message_box(win, ui_tr("Please enter a tag name."), ui_tr("Create Tag"), MB_OK);
      return true;
    }
    if (!gc_create_tag(st->name, st->ref[0] ? st->ref : NULL)) {
      message_box(win, ui_tr("Failed to create tag."), ui_tr("Create Tag"), MB_OK);
      return true;
    }
    end_dialog(win, 1);
    gc_refresh_all();
    return true;
  }
  return false;
}

void gc_show_create_tag_dialog(window_t *parent) {
  create_tag_state_t st = {0};
  show_dialog_from_form(&gc_create_tag_dialog_form, "Create Tag", parent,
                         create_tag_proc, &st);
}
