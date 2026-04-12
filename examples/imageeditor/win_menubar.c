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
  {"Undo", ID_EDIT_UNDO},
  {"Redo", ID_EDIT_REDO},
  {NULL, 0},
  {"Cut", ID_EDIT_CUT},
  {"Copy", ID_EDIT_COPY},
  {"Paste", ID_EDIT_PASTE},
  {NULL, 0},
  {"Clear Selection", ID_EDIT_CLEAR_SEL},
  {"Select All", ID_EDIT_SELECT_ALL},
  {"Deselect", ID_EDIT_DESELECT},
  {NULL, 0},
  {"Crop to Selection", ID_EDIT_CROP},
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

static const menu_item_t kImageItems[] = {
  {"Canvas Size...", ID_IMAGE_RESIZE},
  {NULL, 0},
  {"Flip Horizontal", ID_IMAGE_FLIP_H},
  {"Flip Vertical",   ID_IMAGE_FLIP_V},
  {NULL, 0},
  {"Invert Colors",   ID_IMAGE_INVERT},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

// ── Window menu (dynamic: rebuilt before each popup) ─────────────────────────
// Fixed prefix entries come first; document entries follow the separator.

static const menu_item_t kWindowPrefix[] = {
  {"Tools",  ID_WINDOW_TOOLS},
  {"Colors", ID_WINDOW_COLORS},
  {NULL,     0},   // separator before document list
};
#define WINDOW_PREFIX_COUNT ((int)(sizeof(kWindowPrefix)/sizeof(kWindowPrefix[0])))

// Persistent storage for dynamically built items and document title strings.
static menu_item_t s_window_items[WINDOW_PREFIX_COUNT + WINDOW_MENU_MAX_DOCS];
static int         s_window_item_count = WINDOW_PREFIX_COUNT;

menu_def_t kMenus[] = {
  {"File",   kFileItems,      (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Edit",   kEditItems,      (int)(sizeof(kEditItems)/sizeof(kEditItems[0]))},
  {"Image",  kImageItems,     (int)(sizeof(kImageItems)/sizeof(kImageItems[0]))},
  {"View",   kViewItems,      (int)(sizeof(kViewItems)/sizeof(kViewItems[0]))},
  {"Window", s_window_items,  WINDOW_PREFIX_COUNT},
  {"Help",   kHelpItems,      (int)(sizeof(kHelpItems)/sizeof(kHelpItems[0]))},
};
const int kNumMenus = (int)(sizeof(kMenus)/sizeof(kMenus[0]));

// Index of the Window menu entry in kMenus (must stay in sync with the array).
#define WINDOW_MENU_IDX 4

// Rebuild the Window menu items and re-push the full menu definition to the
// menu-bar window.  Extensibility: add more fixed entries to kWindowPrefix, or
// register new document classes in the loop below.
void window_menu_rebuild(void) {
  if (!g_app || !g_app->menubar_win) return;

  // 1. Copy the fixed prefix entries.
  int n = 0;
  for (int i = 0; i < WINDOW_PREFIX_COUNT; i++)
    s_window_items[n++] = kWindowPrefix[i];

  // 2. One entry per open document, using the window title (filename-only).
  int doc_idx = 0;
  for (canvas_doc_t *d = g_app->docs;
       d && doc_idx < WINDOW_MENU_MAX_DOCS;
       d = d->next, doc_idx++) {
    // The window title is kept up-to-date by doc_update_title(); it outlives
    // this menu as long as the document is open.
    const char *label = (d->win && d->win->title[0]) ? d->win->title : "Untitled";
    s_window_items[n++] = (menu_item_t){ label, (uint16_t)(ID_WINDOW_DOC_BASE + doc_idx) };
  }

  s_window_item_count = n;
  kMenus[WINDOW_MENU_IDX].items      = s_window_items;
  kMenus[WINDOW_MENU_IDX].item_count = s_window_item_count;

  send_message(g_app->menubar_win, kMenuBarMessageSetMenus,
               (uint32_t)kNumMenus, kMenus);
}

static void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;

  switch (id) {
    case ID_FILE_NEW: {
      int w = CANVAS_W, h = CANVAS_H;
      if (show_size_dialog(g_app->menubar_win, "New Image", &w, &h))
        create_document(NULL, w, h);
      break;
    }

    case ID_FILE_OPEN: {
      char path[512] = {0};
      if (show_file_picker(g_app->menubar_win, false, path, sizeof(path))) {
        int img_w = 0, img_h = 0;
        uint8_t *px = load_image(path, &img_w, &img_h);
        if (!px || img_w <= 0 || img_h <= 0) {
          if (px) image_free(px);
          break;
        }
        canvas_doc_t *ndoc = create_document(path, img_w, img_h);
        if (ndoc) {
          // Swap the white placeholder pixels for the actual loaded image.
          // Use image_free() so the allocator always matches regardless of
          // which allocation path (malloc vs stb) produced the buffer.
          image_free(ndoc->pixels);
          ndoc->pixels = px;
          ndoc->canvas_dirty = true;
          ndoc->modified = false;
          doc_update_title(ndoc);
          send_message(ndoc->win, kWindowMessageStatusBar, 0, path);
          invalidate_window(ndoc->canvas_win);
        } else {
          image_free(px);
        }
      }
      break;
    }

    case ID_FILE_SAVE:
      if (!doc) break;
      if (!doc->filename[0]) goto do_save_as;
      if (png_save(doc->filename, doc)) {
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
        if (png_save(path, doc)) {
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
      if (doc) doc_confirm_close(doc, g_app->menubar_win);
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

    case ID_EDIT_CUT:
      if (doc && doc->sel_active) {
        doc_push_undo(doc);
        canvas_cut_selection(doc, g_app->bg_color);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_EDIT_COPY:
      if (doc && doc->sel_active)
        canvas_copy_selection(doc);
      break;

    case ID_EDIT_PASTE:
      if (doc && g_app->clipboard) {
        canvas_paste_clipboard(doc);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_EDIT_CLEAR_SEL:
      if (doc && doc->sel_active) {
        doc_push_undo(doc);
        canvas_clear_selection(doc, g_app->bg_color);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_EDIT_SELECT_ALL:
      if (doc) {
        canvas_select_all(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_EDIT_DESELECT:
      if (doc) {
        canvas_deselect(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_EDIT_CROP:
      if (doc && doc->sel_active) {
        doc_push_undo(doc);
        canvas_crop_to_selection(doc);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_IMAGE_FLIP_H:
      if (doc) {
        doc_push_undo(doc);
        canvas_flip_h(doc);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_IMAGE_FLIP_V:
      if (doc) {
        doc_push_undo(doc);
        canvas_flip_v(doc);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_IMAGE_INVERT:
      if (doc) {
        doc_push_undo(doc);
        canvas_invert_colors(doc);
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_IMAGE_RESIZE: {
      if (!doc) break;
      int new_w = doc->canvas_w, new_h = doc->canvas_h;
      if (show_size_dialog(g_app->menubar_win, "Canvas Size", &new_w, &new_h) &&
          (new_w != doc->canvas_w || new_h != doc->canvas_h)) {
        doc_push_undo(doc);
        canvas_resize(doc, new_w, new_h);
        // Re-read actual dimensions in case resize failed (allocation error)
        if (doc->canvas_win) {
          canvas_win_sync_scrollbars(doc->canvas_win);
          invalidate_window(doc->canvas_win);
        }
        canvas_deselect(doc);
        doc_update_title(doc);
        char sb[32];
        snprintf(sb, sizeof(sb), "%dx%d", doc->canvas_w, doc->canvas_h);
        send_message(doc->win, kWindowMessageStatusBar, 0, sb);
      }
      break;
    }

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
    case ID_TOOL_SELECT:
    case ID_TOOL_HAND:
    case ID_TOOL_ZOOM:
    case ID_TOOL_LINE:
    case ID_TOOL_RECT:
    case ID_TOOL_ELLIPSE:
    case ID_TOOL_ROUNDED_RECT:
    case ID_TOOL_POLYGON:
    case ID_TOOL_SPRAY:
    case ID_TOOL_EYEDROPPER:
    case ID_TOOL_MAGNIFIER:
    case ID_TOOL_TEXT: {
      g_app->current_tool = id;
      // Update the active toolbar button in the tool palette.
      // kToolBarMessageSetActiveButton marks the matching button as active
      // and clears all others, equivalent to the old autoradio_select on child buttons.
      if (g_app->tool_win) {
        send_message(g_app->tool_win, kToolBarMessageSetActiveButton, (uint32_t)id, NULL);
      }
      break;
    }

    case ID_WINDOW_TOOLS:
      // Show and bring the tool palette to front; if hidden, make it visible.
      if (g_app->tool_win) show_window(g_app->tool_win, true);
      break;

    case ID_WINDOW_COLORS:
      // Show and bring the color palette to front.
      if (g_app->color_win) show_window(g_app->color_win, true);
      break;

    default:
      // Window > document entries: activate the n-th open document.
      if (id >= ID_WINDOW_DOC_BASE &&
          id < ID_WINDOW_DOC_BASE + WINDOW_MENU_MAX_DOCS) {
        int target = id - ID_WINDOW_DOC_BASE;
        int i = 0;
        for (canvas_doc_t *d = g_app->docs; d; d = d->next, i++) {
          if (i == target) {
            show_window(d->win, true);
            break;
          }
        }
      }
      break;
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
  // Rebuild the Window menu just before a popup opens so it always reflects
  // the current set of open documents and palette visibility.
  if (msg == kWindowMessageLeftButtonDown) {
    window_menu_rebuild();
  }
  return win_menubar(win, msg, wparam, lparam);
}
