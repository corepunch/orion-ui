// Menu bar window proc and menu command handler

#include "imageeditor.h"

static const menu_item_t kFileItems[] = {
  {"New",        ID_FILE_NEW},
  {"Open...",    ID_FILE_OPEN},
  {NULL,         0},
  {"Save",       ID_FILE_SAVE},
  {"Save As...", ID_FILE_SAVEAS},
  {NULL,         0},
  {"Close",      ID_FILE_CLOSE},
  {NULL,         0},
  {"Quit",       ID_FILE_QUIT},
};

static const menu_item_t kEditItems[] = {
  {"Undo\tCtrl+Z", ID_EDIT_UNDO},
  {"Redo\tCtrl+Y", ID_EDIT_REDO},
};

static const menu_item_t kViewItems[] = {
  {"Zoom In\tCtrl++",  ID_VIEW_ZOOM_IN},
  {"Zoom Out\tCtrl+-", ID_VIEW_ZOOM_OUT},
  {NULL, 0},
  {"1x",               ID_VIEW_ZOOM_1X},
  {"2x",               ID_VIEW_ZOOM_2X},
  {"4x",               ID_VIEW_ZOOM_4X},
  {"6x",               ID_VIEW_ZOOM_6X},
  {"8x",               ID_VIEW_ZOOM_8X},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

const menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Edit", kEditItems, (int)(sizeof(kEditItems)/sizeof(kEditItems[0]))},
  {"View", kViewItems, (int)(sizeof(kViewItems)/sizeof(kViewItems[0]))},
  {"Help", kHelpItems, (int)(sizeof(kHelpItems)/sizeof(kHelpItems[0]))},
};

static void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;

  switch (id) {
    case ID_FILE_NEW:
      create_document(NULL);
      break;

    case ID_FILE_OPEN: {
      char path[512] = {0};
      if (show_file_picker(g_app->menubar_win, false, path, sizeof(path))) {
        canvas_doc_t *ndoc = create_document(path);
        if (ndoc) {
          if (!png_load(path, ndoc->pixels)) {
            send_message(ndoc->win, kWindowMessageStatusBar, 0,
                         (void *)"Failed to open file");
          } else {
            ndoc->canvas_dirty = true;
            ndoc->modified = false;
            doc_update_title(ndoc);
            send_message(ndoc->win, kWindowMessageStatusBar, 0, path);
            invalidate_window(ndoc->canvas_win);
          }
        }
      }
      break;
    }

    case ID_FILE_SAVE:
      if (!doc) break;
      if (!doc->filename[0]) goto do_save_as;
      if (png_save(doc->filename, doc->pixels)) {
        doc->modified = false;
        doc_update_title(doc);
        send_message(doc->win, kWindowMessageStatusBar, 0, (void *)"Saved");
      } else {
        send_message(doc->win, kWindowMessageStatusBar, 0, (void *)"Save failed");
      }
      break;

    do_save_as:
    case ID_FILE_SAVEAS: {
      if (!doc) break;
      char path[512] = {0};
      if (show_file_picker(g_app->menubar_win, true, path, sizeof(path))) {
        strncpy(doc->filename, path, sizeof(doc->filename)-1);
        doc->filename[sizeof(doc->filename)-1] = '\0';
        if (png_save(path, doc->pixels)) {
          doc->modified = false;
          doc_update_title(doc);
          send_message(doc->win, kWindowMessageStatusBar, 0, path);
        } else {
          send_message(doc->win, kWindowMessageStatusBar, 0, (void *)"Save failed");
        }
      }
      break;
    }

    case ID_FILE_CLOSE:
      if (doc) close_document(doc);
      break;

    case ID_FILE_QUIT:
      running = false;
      break;

    case ID_EDIT_UNDO:
      if (doc && doc_undo(doc)) {
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_EDIT_REDO:
      if (doc && doc_redo(doc)) {
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_HELP_ABOUT:
      show_about_dialog(g_app->menubar_win);
      break;

    case ID_VIEW_ZOOM_IN:
    case ID_VIEW_ZOOM_OUT:
    case ID_VIEW_ZOOM_1X:
    case ID_VIEW_ZOOM_2X:
    case ID_VIEW_ZOOM_4X:
    case ID_VIEW_ZOOM_6X:
    case ID_VIEW_ZOOM_8X: {
      if (!doc || !doc->canvas_win) break;
      canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
      if (!state) break;

      int new_scale = state->scale;

      if (id == ID_VIEW_ZOOM_IN) {
        for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
          if (kZoomLevels[i] > state->scale) { new_scale = kZoomLevels[i]; break; }
        }
      } else if (id == ID_VIEW_ZOOM_OUT) {
        for (int i = NUM_ZOOM_LEVELS - 1; i >= 0; i--) {
          if (kZoomLevels[i] < state->scale) { new_scale = kZoomLevels[i]; break; }
        }
      } else {
        for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
          if (kZoomMenuIDs[i] == (int)id) { new_scale = kZoomLevels[i]; break; }
        }
      }

      canvas_win_set_zoom(doc->canvas_win, new_scale);

      // Update status bar with current zoom
      char zoom_msg[32];
      snprintf(zoom_msg, sizeof(zoom_msg), "Zoom: %dx", new_scale);
      send_message(doc->win, kWindowMessageStatusBar, 0, zoom_msg);
      break;
    }

    case ID_TOOL_PENCIL:
    case ID_TOOL_BRUSH:
    case ID_TOOL_ERASER:
    case ID_TOOL_FILL:
    case ID_TOOL_SELECT: {
      g_app->current_tool = id;
      // Update the active toolbar button in the tool palette.
      // kToolBarMessageSetActiveButton marks the matching button as active
      // and clears all others, equivalent to the old autoradio_select on child buttons.
      if (g_app->tool_win) {
        send_message(g_app->tool_win, kToolBarMessageSetActiveButton, (uint32_t)id, NULL);
      }
      break;
    }
  }
}

result_t editor_menubar_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  if (msg == kWindowMessageCommand) {
    uint16_t notif = HIWORD(wparam);
    if (notif == kMenuBarNotificationItemClick ||
        notif == kAcceleratorNotification      ||
        notif == kButtonNotificationClicked) {
      handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  return win_menubar(win, msg, wparam, lparam);
}
