// Remote management dialog — uses generated form from gitclient.orion.

#include "gitclient.h"

static void refresh_remote_list(window_t *win) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return;
  window_t *cb = get_window_item(win, ID_REMOTE_DIALOG_REMOTE_LIST);
  send_message(cb, cbClear, 0, NULL);
  char remotes[8][256];
  int n = git_get_remotes(gc->repo, remotes, 8);
  for (int i = 0; i < n; i++)
    send_message(cb, cbAddString, 0, remotes[i]);
}

static void populate_fields_from_selection(window_t *win) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return;
  window_t *cb = get_window_item(win, ID_REMOTE_DIALOG_REMOTE_LIST);
  char name[256] = {0};
  send_message(cb, edGetText, sizeof(name), name);
  if (!name[0]) return;
  set_window_item_text(win, ID_REMOTE_DIALOG_NAME, "%s", name);
  char url[512] = {0};
  git_get_remote_url(gc->repo, name, url, sizeof(url));
  set_window_item_text(win, ID_REMOTE_DIALOG_URL, "%s", url);
}

static result_t remote_dlg_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      refresh_remote_list(win);
      return true;

    case evCommand: {
      uint16_t code = HIWORD(wparam);
      if (code == cbSelectionChange) {
        window_t *src = (window_t *)lparam;
        if (src && src->id == ID_REMOTE_DIALOG_REMOTE_LIST) {
          populate_fields_from_selection(win);
          return true;
        }
        return false;
      }
      if (code == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;

        if (src->id == ID_REMOTE_DIALOG_CLOSE) {
          end_dialog(win, 0);
          return true;
        }

        gc_state_t *gc = g_gc;
        if (!gc || !gc->repo) return true;

        if (src->id == ID_REMOTE_DIALOG_ADD) {
          char name[256] = {0};
          char url[512] = {0};
          send_message(get_window_item(win, ID_REMOTE_DIALOG_NAME),
                       edGetText, sizeof(name), name);
          send_message(get_window_item(win, ID_REMOTE_DIALOG_URL),
                       edGetText, sizeof(url), url);
          if (!name[0] || !url[0]) {
            message_box(win, ui_tr("Please enter both name and URL."), ui_tr("Add Remote"), MB_OK);
            return true;
          }
          if (!gc_add_remote(name, url))
            message_box(win, ui_tr("Failed to add remote."), ui_tr("Add Remote"), MB_OK);
          refresh_remote_list(win);
          return true;
        }

        if (src->id == ID_REMOTE_DIALOG_UPDATE) {
          char name[256] = {0};
          char url[512] = {0};
          send_message(get_window_item(win, ID_REMOTE_DIALOG_NAME),
                       edGetText, sizeof(name), name);
          send_message(get_window_item(win, ID_REMOTE_DIALOG_URL),
                       edGetText, sizeof(url), url);
          if (!name[0]) {
            message_box(win, ui_tr("Please enter the remote name."), ui_tr("Update Remote"), MB_OK);
            return true;
          }
          if (!url[0]) {
            message_box(win, ui_tr("Please enter the remote URL."), ui_tr("Update Remote"), MB_OK);
            return true;
          }
          if (!gc_set_remote_url(name, url))
            message_box(win, ui_tr("Failed to update remote."), ui_tr("Update Remote"), MB_OK);
          refresh_remote_list(win);
          return true;
        }

        if (src->id == ID_REMOTE_DIALOG_REMOVE) {
          char name[256] = {0};
          send_message(get_window_item(win, ID_REMOTE_DIALOG_NAME),
                       edGetText, sizeof(name), name);
          if (!name[0]) {
            message_box(win, ui_tr("Please enter the remote name."), ui_tr("Remove Remote"), MB_OK);
            return true;
          }
          char msg[320];
          snprintf(msg, sizeof(msg), ui_tr("Remove remote \"%s\"?"), name);
          if (message_box(win, msg, "Remove Remote", MB_YESNO) != IDYES)
            return true;
          if (!gc_remove_remote(name))
            message_box(win, ui_tr("Failed to remove remote."), ui_tr("Remove Remote"), MB_OK);
          refresh_remote_list(win);
          return true;
        }
      }
      return false;
    }

    default:
      return false;
  }
}

void gc_show_remote_dialog(window_t *parent) {
  show_dialog_from_form(&gc_remote_dialog_form, "Manage Remotes", parent,
                         remote_dlg_proc, NULL);
}
