// Menu bar window proc and menu command handler

#include "imageeditor.h"
#include "../commands/commands.h"

#define VIEW_ITEM_COUNT ((int)(sizeof(MENU_VIEW_ITEMS) / sizeof(MENU_VIEW_ITEMS[0])))
#define WINDOW_PREFIX_COUNT ((int)(sizeof(MENU_WINDOW_ITEMS) / sizeof(MENU_WINDOW_ITEMS[0])))

#if !IMAGEEDITOR_INDEXED
#define FILTER_PREFIX_COUNT ((int)(sizeof(MENU_FILTER_ITEMS) / sizeof(MENU_FILTER_ITEMS[0])))
static menu_item_t s_filter_items[FILTER_PREFIX_COUNT + 1 + IMAGEEDITOR_MAX_FILTERS];
static int         s_filter_item_count = FILTER_PREFIX_COUNT;
static menu_item_t s_filter_photo_items[IMAGEEDITOR_MAX_FILTERS];
static int         s_filter_photo_item_count = 0;
static char        s_filter_photo_labels[IMAGEEDITOR_MAX_FILTERS][64];
#endif // !IMAGEEDITOR_INDEXED

// Persistent storage for dynamically built items and document title strings.
static menu_item_t s_edit_items[ARRAY_LEN(MENU_EDIT_ITEMS)];
static menu_item_t s_view_items[VIEW_ITEM_COUNT];
static bool        s_view_items_initialized = false;
static menu_item_t s_window_items[WINDOW_PREFIX_COUNT + WINDOW_MENU_MAX_DOCS];
static int         s_window_item_count = WINDOW_PREFIX_COUNT;
static int         s_last_blur_radius = 4;

static menu_def_t *find_menu(const char *label) {
  for (int i = 0; i < kNumMenus; i++) {
    if (kMenus[i].label && strcmp(kMenus[i].label, label) == 0)
      return &kMenus[i];
  }
  return NULL;
}

static void publish_dynamic_menus(void) {
  if (g_app && g_app->menubar_win) {
    send_message(g_app->menubar_win, kMenuBarMessageSetMenus,
                 (uint32_t)kNumMenus, kMenus);
  }
}


static bool cancel_active_canvas_interaction(canvas_doc_t *doc, int old_tool) {
  bool changed = false;
  canvas_win_state_t *state;

  if (!doc) return false;

  state = doc->canvas_win ? (canvas_win_state_t *)doc->canvas_win->userdata : NULL;
  if (state && state->pan.active) {
    IE_DEBUG("cancel_interaction pan doc=%p old_tool=%s",
             (void *)doc, tool_id_name(old_tool));
    state->pan.active = false;
    changed = true;
  }

  if (old_tool == ID_TOOL_POLYGON && doc->poly.active) {
    IE_DEBUG("cancel_interaction polygon doc=%p points=%d",
             (void *)doc, doc->poly.count);
    if (doc->shape.snapshot) {
      memcpy(doc->pixels, doc->shape.snapshot,
             (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
      doc->canvas_dirty = true;
    }
    ie_doc_commit_op(doc, false);
    doc->poly.active = false;
    doc->poly.count = 0;
    changed = true;
  }

  if (canvas_is_shape_tool(old_tool) && doc->drawing && doc->shape.snapshot) {
    IE_DEBUG("cancel_interaction shape doc=%p tool=%s start=(%d,%d) last=(%d,%d)",
             (void *)doc, tool_id_name(old_tool),
             doc->shape.start.x, doc->shape.start.y,
             doc->last.x, doc->last.y);
    memcpy(doc->pixels, doc->shape.snapshot,
           (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
    doc->canvas_dirty = true;
    ie_doc_commit_op(doc, false);
    changed = true;
  }

  if (doc->sel.move.active) {
    IE_DEBUG("cancel_interaction selection_move doc=%p float_pos=(%d,%d)",
             (void *)doc, doc->sel.floating.rect.x, doc->sel.floating.rect.y);
    canvas_commit_move(doc);
    changed = true;
  }

  if (doc->sel.move.mask_moving) {
    IE_DEBUG("cancel_interaction selection_mask_move doc=%p", (void *)doc);
    canvas_commit_selection_mask_offset(doc);
    doc->sel.move.mask_moving = false;
    changed = true;
  }

  if (old_tool == ID_TOOL_CROP && doc->sel.active) {
    IE_DEBUG("cancel_interaction crop doc=%p", (void *)doc);
    if (doc->command.before) ie_doc_commit_op(doc, false);
    else canvas_deselect(doc);
    changed = true;
  }

  if (doc->drawing) {
    canvas_stroke_end(doc, doc->last);
    IE_DEBUG("cancel_interaction drawing doc=%p old_tool=%s",
             (void *)doc, tool_id_name(old_tool));
    doc->drawing = false;
    changed = true;
  }

  ie_doc_commit_op(doc, true);
  return changed;
}

void anim_stop_playback(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return;
  anim_timeline_t *tl = doc->anim;
  bool was_playing = tl->playing;
  tl->playing = false;
  if (g_app && g_app->anim_timer_id) {
    axCancelTimer(g_app->anim_timer_id);
    g_app->anim_timer_id = 0;
  }
  if (was_playing) {
    IE_TRACE("playback stop win=%p frame=%d restore=%d", (void *)doc->canvas_win,
             tl->active_frame, tl->playback_start_frame);
    if (anim_timeline_switch_frame(tl, tl->playback_start_frame, &doc->pixels,
                                    doc->canvas_w, doc->canvas_h, IE_FRAME_FORMAT)) {
      if (doc->layer.count > 0)
        doc->layer.stack[doc->layer.active]->pixels = doc->pixels;
      doc->canvas_dirty = true;
    } else {
      IE_TRACE("playback restore failed win=%p frame=%d target=%d count=%d",
               (void *)doc->canvas_win, tl->active_frame, tl->playback_start_frame, tl->frame_count);
    }
    if (doc->canvas_win) invalidate_window(doc->canvas_win);
    timeline_win_refresh();
  } else {
    timeline_toolbar_sync();
  }
}

static bool anim_step_frame(canvas_doc_t *doc, int delta) {
  if (!doc || !doc->anim || delta == 0) return false;
  anim_stop_playback(doc);
  anim_timeline_t *tl = doc->anim;
  int target = tl->active_frame + delta;
  if (target < 0 || target >= tl->frame_count) return false;
  return cmd_frame_select(doc, target);
}

// ============================================================
// Palette window helpers (shared by gem_init / handle_menu_command)
// ============================================================

window_t *create_tool_palette_window(void) {
  if (!g_app) return NULL;
  if (g_app->tool_win) return g_app->tool_win;
  if (!g_app->chrome_win) {
    g_app->chrome_win = create_application_chrome("Image Editor Chrome", NULL, NULL, 0,
                                          main_toolbar_proc, &imageeditor_application_toolbar, g_app->hinstance);
    g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  }
  window_t *tp = app_chrome_add_toolbar(g_app->chrome_win, TOOLBAR_DOCK_LEFT, win_tool_palette_proc);
  IE_TRACE("attach tools toolbar win=%p chrome=%p", (void *)tp, (void *)g_app->chrome_win);
  g_app->tool_win = tp;
  return tp;
}

window_t *create_tool_options_window(void) {
  if (!g_app) return NULL;
  if (g_app->tool_options_win) return g_app->tool_options_win;
  window_t *tw = create_window(
      "Options",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_NOCLOSE | WINDOW_NOTITLE | WINDOW_TOOLBAR,
      MAKERECT(TOOL_OPTIONS_WIN_X, TOOL_OPTIONS_WIN_Y,
               TOOL_OPTIONS_WIN_W, TOOL_OPTIONS_WIN_H),
      NULL, win_tool_options_proc, g_app->hinstance, NULL);
  show_window(tw, true);
  g_app->tool_options_win = tw;
  imageeditor_sync_tool_options();
  return tw;
}

window_t *create_color_palette_window(void) {
  window_t *cp = create_window(
      "Colors",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(COLOR_WIN_X, COLOR_WIN_Y, COLOR_WIN_W, COLOR_WIN_H),
      NULL, win_color_palette_proc, g_app->hinstance, NULL);
  show_window(cp, true);
  g_app->color_win = cp;
  return cp;
}

result_t main_toolbar_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      imageeditor_sync_main_toolbar();
      return true;
    case tbButtonClick:
      handle_menu_command((uint16_t)wparam);
      imageeditor_sync_main_toolbar();
      return true;
    case evDestroy:
      if (g_app && g_app->main_toolbar_win == win) g_app->main_toolbar_win = NULL;
      return false;
    default:
      return false;
  }
}

window_t *create_main_toolbar_window(void) {
  if (!g_app) return NULL;
  if (g_app->chrome_win) return app_chrome_toolbar(g_app->chrome_win);
  g_app->chrome_win = create_application_chrome("Image Editor Chrome", NULL, NULL, 0,
                                        main_toolbar_proc, &imageeditor_application_toolbar, g_app->hinstance);
  window_t *win = app_chrome_toolbar(g_app->chrome_win);
  g_app->main_toolbar_win = win;
  imageeditor_sync_main_toolbar();
  return win;
}

void imageeditor_sync_main_toolbar(void) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;
  bool undo = doc && !doc->command.before && doc->undo.count > 0;
  bool redo = doc && !doc->command.before && doc->redo.count > 0;
  memcpy(s_edit_items, MENU_EDIT_ITEMS, sizeof(s_edit_items));
  for (int i = 0; i < ARRAY_LEN(s_edit_items); i++) {
    if (s_edit_items[i].id == ID_EDIT_UNDO) s_edit_items[i].disabled = !undo;
    if (s_edit_items[i].id == ID_EDIT_REDO) s_edit_items[i].disabled = !redo;
  }
  menu_def_t *edit = find_menu("Edit");
  if (edit) edit->items = s_edit_items;
  publish_dynamic_menus();
  if (!g_app->main_toolbar_win) return;
  send_message(g_app->main_toolbar_win, tbEnableItem, ID_EDIT_UNDO, (void *)(intptr_t)undo);
  send_message(g_app->main_toolbar_win, tbEnableItem, ID_EDIT_REDO, (void *)(intptr_t)redo);
  window_t *mask_btn = get_window_item(g_app->main_toolbar_win, ID_VIEW_MASK_ONLY);
  window_t *bg_btn   = get_window_item(g_app->main_toolbar_win, ID_VIEW_SHOW_BACKGROUND);

  if (mask_btn) {
    bool checked = g_app->active_doc && g_app->active_doc->layer.mask_only_view;
    send_message(mask_btn, btnSetCheck, checked ? btnStateChecked : btnStateUnchecked, NULL);
  }

  if (bg_btn) {
    bool checked = !g_app->active_doc || g_app->active_doc->background.show;
    send_message(bg_btn, btnSetCheck, checked ? btnStateChecked : btnStateUnchecked, NULL);
    send_message(bg_btn, btnSetIconName, 0, (void *)(checked ? "eye" : "eye-closed"));
  }
}

// Prefix strings for toggleable menu items.
// [x] = currently enabled, [ ] = currently disabled.
#define MENU_CHECK_ON  "[x] "
#define MENU_CHECK_OFF "[ ] "

// Update the View menu's toggleable items to reflect the current grid state.
// Called before each popup open so the labels always show current state.
static void view_menu_rebuild(void) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;
  if (!s_view_items_initialized) {
    memcpy(s_view_items, MENU_VIEW_ITEMS, sizeof(s_view_items));
    s_view_items_initialized = true;
  }
  int n = (int)(sizeof(s_view_items) / sizeof(s_view_items[0]));
  for (int i = 0; i < n; i++) {
    if (s_view_items[i].id == ID_VIEW_SHOW_GRID)
      s_view_items[i].label = g_app->grid.visible
                                 ? MENU_CHECK_ON "Show Grid"
                                 : MENU_CHECK_OFF "Show Grid";
    if (s_view_items[i].id == ID_VIEW_SNAP_GRID)
      s_view_items[i].label = g_app->grid.snap
                                 ? MENU_CHECK_ON "Snap to Grid"
                                 : MENU_CHECK_OFF "Snap to Grid";
    if (s_view_items[i].id == ID_VIEW_SHOW_BACKGROUND)
      s_view_items[i].label = (!doc || doc->background.show)
                                 ? MENU_CHECK_ON "Show Background"
                                 : MENU_CHECK_OFF "Show Background";
    if (s_view_items[i].id == ID_VIEW_MASK_ONLY)
      s_view_items[i].label = (doc && doc->layer.mask_only_view)
                                 ? MENU_CHECK_ON "Mask Only View"
                                 : MENU_CHECK_OFF "Mask Only View";
  }

  menu_def_t *view_menu = find_menu("View");
  if (view_menu) {
    view_menu->items = s_view_items;
    view_menu->item_count = n;
  }
  publish_dynamic_menus();
}

void window_menu_rebuild(void) {
  if (!g_app) return;

  int n = 0;
  for (int i = 0; i < WINDOW_PREFIX_COUNT; i++)
    s_window_items[n++] = MENU_WINDOW_ITEMS[i];

  int doc_idx = 0;
  for (canvas_doc_t *d = g_app->docs;
       d && doc_idx < WINDOW_MENU_MAX_DOCS;
       d = d->next, doc_idx++) {
    // The window title is kept up-to-date by doc_update_title(); it outlives
    // this menu as long as the document is open.
    const char *label = (d->win && d->win->title[0]) ? d->win->title : "Untitled";
    s_window_items[n++] = (menu_item_t){ label, (uint16_t)(ID_WINDOW_DOC_BASE + doc_idx), NULL, 0 };
  }

  s_window_item_count = n;
  menu_def_t *window_menu = find_menu("Window");
  if (window_menu) {
    window_menu->items = s_window_items;
    window_menu->item_count = s_window_item_count;
  }

  // In standalone mode push the updated menus to our local menubar window.
  // In gem mode menubar_win is NULL; the shell holds a pointer to kMenus
  // (set during gem_init) and will read the updated data on its next redraw.
  publish_dynamic_menus();
}

bool imageeditor_open_file_path(const char *path) {
  if (!g_app || !path || !path[0]) return false;

  IE_TRACE("open path=%s", path);
  int img_w = 0, img_h = 0;
#if IMAGEEDITOR_INDEXED
  uint32_t pal[256] = {0};
  uint32_t background = IE_PAPER_COLOR;
  bool show_bg = true;
  anim_timeline_t *loaded_anim = NULL;
  int pal_count = 0;
  uint8_t *px = NULL;
  if (flc_is_file(path)) {
    loaded_anim = flc_load(path, &img_w, &img_h, pal, &background, &show_bg);
    if (!loaded_anim) return false;
    pal_count = 256;
    px = malloc((size_t)img_w * img_h);
    if (px) memcpy(px, loaded_anim->frames[0]->data, (size_t)img_w * img_h);
  } else px = image_io_load(path, &img_w, &img_h, pal, &pal_count);
#else
  uint8_t *px = image_io_load(path, &img_w, &img_h, NULL, NULL);
#endif
  if (!px || img_w <= 0 || img_h <= 0) {
    free(px);
#if IMAGEEDITOR_INDEXED
    anim_timeline_free(loaded_anim);
#endif
    return false;
  }

  canvas_doc_t *ndoc = create_document_pixels(path, img_w, img_h);
  if (!ndoc) {
    free(px);
#if IMAGEEDITOR_INDEXED
    anim_timeline_free(loaded_anim);
#endif
    return false;
  }

  // Swap the transparent placeholder pixels for the actual loaded image.
  // Update both the layer buffer and the convenience alias.
  free(ndoc->layer.stack[0]->pixels);
  ndoc->layer.stack[0]->pixels = px;
  ndoc->pixels = px;
  ndoc->canvas_dirty = true;
  ndoc->modified = false;

#if IMAGEEDITOR_INDEXED
  // Overwrite the default palette with the one embedded in the file.
  if (pal_count > 0) {
    memcpy(ndoc->ipal.entries, pal, (size_t)pal_count * sizeof(uint32_t));
    ndoc->ipal.count = pal_count;
  }
  if (loaded_anim) {
    anim_timeline_free(ndoc->anim);
    ndoc->anim = loaded_anim;
    ndoc->background.color = background;
    ndoc->background.show = show_bg;
  }
#endif

  // Sync the animation frame 0 with the loaded pixels so the thumbnail is
  // accurate from the start.
  if (ndoc->anim && ndoc->anim->frame_count > 0)
    anim_frame_compress(ndoc->anim->frames[0], px, img_w, img_h,
#if IMAGEEDITOR_INDEXED
                        FRAME_FORMAT_INDEXED);
#else
                        FRAME_FORMAT_RGBA);
#endif
  doc_update_title(ndoc);
  send_message(ndoc->win, evStatusBar, 0, (void *)path);
  // Large images open in a bird's-eye view using the maximum reasonable
  // center workspace, but small images stay at 1x instead of being enlarged.
  int max_view_w = 1;
  int max_view_h = 1;
  imageeditor_max_canvas_viewport_size(&max_view_w, &max_view_h);
  float open_scale = imageeditor_fit_scale_for_viewport(img_w, img_h,
                                                        max_view_w, max_view_h,
                                                        false);
  int wrapped_view_w = MAX(1, (int)lroundf((float)img_w * open_scale));
  int wrapped_view_h = MAX(1, (int)lroundf((float)img_h * open_scale));
  int wrapped_frame_w = 1;
  int wrapped_frame_h = 1;
  imageeditor_document_frame_for_viewport(wrapped_view_w, wrapped_view_h,
                                          &wrapped_frame_w, &wrapped_frame_h);
  resize_window(ndoc->win, wrapped_frame_w, wrapped_frame_h);
  canvas_win_set_scale(ndoc->canvas_win, open_scale);
  invalidate_window(ndoc->canvas_win);
  timeline_win_refresh();
  return true;
}

static bool save_document_file(canvas_doc_t *doc, const char *path) {
  anim_stop_playback(doc);
  if (!image_io_save(path, doc)) {
    IE_TRACE("save failed doc=%p path=%s", (void *)doc, path);
    message_box(doc->win, "Could not save the document. Check the filename, available space,\nand that no drawing operation is in progress.", "Save failed", MB_OK);
    return false;
  }
  if (path != doc->filename) snprintf(doc->filename, sizeof(doc->filename), "%s", path);
  doc->modified = false;
  doc_update_title(doc);
  send_message(doc->win, evStatusBar, 0, (void *)"Saved");
  return true;
}

void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;
  // If no document has focus, fall back to the first available document
  if (!doc) doc = g_app->docs;

  IE_TRACE("command dispatch doc=%p id=%u", (void *)doc, id);
  switch (id) {
    case ID_FILE_NEW: {
      int w, h;
      imageeditor_default_canvas_size(&w, &h);
      if (show_size_dialog(g_app->menubar_win, "New Image", &w, &h))
        create_document(NULL, w, h);
      break;
    }

    case ID_FILE_OPEN: {
      char path[512] = {0};
      if (show_file_picker(g_app->menubar_win, false, path, sizeof(path))) {
        if (!imageeditor_open_file_path(path))
          message_box(g_app->menubar_win, "Could not open this file. It may be damaged or use an unsupported format.", "Open failed", MB_OK);
      }
      break;
    }

    case ID_FILE_SAVE:
      if (!doc) break;
      if (!doc->filename[0]) goto do_save_as;
#if IMAGEEDITOR_BW
      const char *ext = strrchr(doc->filename, '.');
      if (!ext || strcasecmp(ext, ".flc")) goto do_save_as;
#endif
      save_document_file(doc, doc->filename);
      break;

    do_save_as:
    case ID_FILE_SAVEAS: {
      if (!doc) break;
      char path[512];
      snprintf(path, sizeof(path), "%s", doc->filename);
#if IMAGEEDITOR_BW
      char *ext = strrchr(path, '.'), *slash = strrchr(path, '/');
      if (ext && (!slash || ext > slash)) *ext = 0;
      if (!path[0]) snprintf(path, sizeof(path), "Untitled");
      if (strlen(path) + 4 >= sizeof(path)) break;
      strcat(path, ".flc");
#endif
      if (show_file_picker(g_app->menubar_win, true, path, sizeof(path))) {
        save_document_file(doc, path);
      }
      break;
    }

    case ID_FILE_CLOSE:
      if (doc) doc_confirm_close(doc, g_app->menubar_win);
      break;

    case ID_FILE_QUIT:
#ifdef BUILD_AS_GEM
      // In gem mode ui_request_quit() is a no-op.  Destroy all gem-owned
      // windows so no window procs remain pointing into unloaded code.
      if (g_app) {
        // Destroy all document windows first.
        for (canvas_doc_t *d = g_app->docs, *next = NULL; d; d = next) {
          next = d->next;
          if (d->win) destroy_window(d->win);
        }
        if (g_app->color_win) destroy_window(g_app->color_win);
        if (g_app->tool_win)  destroy_window(g_app->tool_win);
      }
#else
      ui_request_quit();
#endif
      break;

    case ID_EDIT_UNDO:
      cmd_undo(doc);
      break;

    case ID_EDIT_REDO:
      cmd_redo(doc);
      break;

    case ID_EDIT_CUT:
      cmd_cut(doc);
      break;

    case ID_EDIT_COPY:
      cmd_copy(doc);
      break;

    case ID_EDIT_PASTE:
      cmd_paste(doc);
      break;

    case ID_SELECT_CLEAR:
      cmd_select_clear(doc);
      break;

    case ID_SELECT_ALL:
      cmd_select_all(doc);
      break;

    case ID_SELECT_DESELECT:
      cmd_deselect(doc);
      break;

    case ID_SELECT_EXPAND:
      if (doc && doc->sel.active) {
        int amount = 1;
        if (show_selection_modify_dialog(doc->win ? doc->win : g_app->menubar_win,
                                         "Expand Selection", &amount))
          cmd_select_expand(doc, amount);
      }
      break;

    case ID_SELECT_CONTRACT:
      if (doc && doc->sel.active) {
        int amount = 1;
        if (show_selection_modify_dialog(doc->win ? doc->win : g_app->menubar_win,
                                         "Contract Selection", &amount))
          cmd_select_contract(doc, amount);
      }
      break;

    case ID_IMAGE_CROP:
      cmd_crop_to_selection(doc);
      break;

    case ID_IMAGE_FLIP_H:
      cmd_flip_horizontal(doc);
      break;

    case ID_IMAGE_FLIP_V:
      cmd_flip_vertical(doc);
      break;

    case ID_IMAGE_INVERT:
      cmd_invert_colors(doc);
      break;

#if !IMAGEEDITOR_INDEXED
    // Levels is GPU/shader-based; in indexed mode it would need to remap
    // palette entries instead of pixel data (future work).
    case ID_IMAGE_LEVELS:
      if (doc && doc->layer.active >= 0 && doc->layer.active < doc->layer.count) {
        if (!ie_doc_begin_op(doc, "Levels")) break;
        ie_doc_commit_op(doc, show_levels_dialog(doc->win ? doc->win : g_app->menubar_win));
      }
      break;
#endif // !IMAGEEDITOR_INDEXED

#if !IMAGEEDITOR_INDEXED
    case ID_FILTER_RELOAD:
      imageeditor_load_filters();
      break;

    case ID_FILTER_GALLERY:
      if (doc && show_filter_gallery_dialog(doc->win ? doc->win : g_app->menubar_win)) {
        doc_update_title(doc);
        if (doc->canvas_win)
          invalidate_window(doc->canvas_win);
      }
      break;

    case ID_FILTER_BLUR:
      if (doc && doc->layer.active >= 0 && doc->layer.active < doc->layer.count) {
        int amount = s_last_blur_radius;
        if (show_blur_dialog(doc->win ? doc->win : g_app->menubar_win, &amount)) {
          s_last_blur_radius = amount;
          if (!ie_doc_begin_op(doc, "Blur")) break;
          ie_doc_commit_op(doc, imageeditor_apply_builtin_blur(doc, amount));
          doc_update_title(doc);
          if (doc->canvas_win)
            invalidate_window(doc->canvas_win);
        }
      }
      break;

    case ID_FILTER_SHARPEN:
    case ID_FILTER_EDGE:
      if (doc) {
        if (!ie_doc_begin_op(doc, "Filter")) break;
        ie_doc_commit_op(doc, imageeditor_apply_builtin_filter(doc, id));
        doc_update_title(doc);
        if (doc->canvas_win)
          invalidate_window(doc->canvas_win);
      }
      break;
#endif // !IMAGEEDITOR_INDEXED

    case ID_IMAGE_RESIZE: {
      if (!doc) break;
      int new_w = doc->canvas_w, new_h = doc->canvas_h;
      image_resize_filter_t filter = IMAGE_RESIZE_BILINEAR;
      if (show_image_resize_dialog(doc->win ? doc->win : g_app->menubar_win,
                                   &new_w, &new_h, &filter) &&
          (new_w != doc->canvas_w || new_h != doc->canvas_h)) {
        cmd_resize_image(doc, new_w, new_h, filter);
        canvas_deselect(doc);
        char sb[32];
        snprintf(sb, sizeof(sb), "%dx%d", doc->canvas_w, doc->canvas_h);
        send_message(doc->win, evStatusBar, 0, sb);
      }
      break;
    }

    case ID_IMAGE_CANVAS_SIZE: {
      if (!doc) break;
      int new_w = doc->canvas_w, new_h = doc->canvas_h;
      if (show_size_dialog(doc->win ? doc->win : g_app->menubar_win,
                           "Canvas Size", &new_w, &new_h) &&
          (new_w != doc->canvas_w || new_h != doc->canvas_h)) {
        cmd_resize_canvas(doc, new_w, new_h);
        canvas_deselect(doc);
        char sb[32];
        snprintf(sb, sizeof(sb), "%dx%d", doc->canvas_w, doc->canvas_h);
        send_message(doc->win, evStatusBar, 0, sb);
      }
      break;
    }

    case ID_HELP_ABOUT:
      show_about_dialog(g_app->menubar_win);
      break;

    case ID_VIEW_WINDOW_MODE:
      if (g_app->active_doc) {
        window_t *host = g_app->active_doc->win;
        IE_TRACE("window mode win=%p maximized=%d", (void *)host, host->maximized);
        if (host->maximized) restore_window(host);
        else maximize_window(host);
      }
      break;
    case ID_VIEW_ZOOM_IN:
    case ID_VIEW_ZOOM_OUT:
    case ID_VIEW_ZOOM_FIT:
    case ID_VIEW_ZOOM_1X:
    case ID_VIEW_ZOOM_2X:
    case ID_VIEW_ZOOM_4X:
    case ID_VIEW_ZOOM_6X:
    case ID_VIEW_ZOOM_8X:
      imageeditor_handle_zoom_command(doc, id);
      break;

    case ID_VIEW_SHOW_GRID:
      g_app->grid.visible = !g_app->grid.visible;
      if (doc && doc->canvas_win) invalidate_window(doc->canvas_win);
      break;

    case ID_VIEW_SNAP_GRID:
      g_app->grid.snap = !g_app->grid.snap;
      break;

    case ID_VIEW_GRID_OPTIONS: {
      int gx = g_app->grid.spacing.x > 0 ? g_app->grid.spacing.x : 8;
      int gy = g_app->grid.spacing.y > 0 ? g_app->grid.spacing.y : 8;
      if (show_grid_options_dialog(g_app->menubar_win, &gx, &gy)) {
        g_app->grid.spacing.x = gx;
        g_app->grid.spacing.y = gy;
        if (doc && doc->canvas_win && g_app->grid.visible)
          invalidate_window(doc->canvas_win);
      }
      break;
    }

    case ID_VIEW_MASK_ONLY:
      if (doc) {
        doc_set_mask_only_view(doc, !doc->layer.mask_only_view);
        if (doc->canvas_win)
          invalidate_window(doc->canvas_win);
        view_menu_rebuild();
      }
      break;

    case ID_VIEW_SHOW_BACKGROUND:
      if (doc) {
        doc->background.show = !doc->background.show;
        if (doc->canvas_win)
          invalidate_window(doc->canvas_win);
        if (g_app->timeline_win)
          invalidate_window(g_app->timeline_win);
        imageeditor_sync_main_toolbar();
        view_menu_rebuild();
      }
      break;

    case ID_TOOL_PENCIL:
    case ID_TOOL_BRUSH:
    case ID_TOOL_ERASER:
    case ID_TOOL_FILL:
    case ID_TOOL_SELECT:
    case ID_TOOL_CROP:
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
    case ID_TOOL_TEXT:
    case ID_TOOL_MAGIC_WAND:
    case ID_TOOL_MOVE: {
      int old_tool = g_app->current_tool;
      if (doc && old_tool != (int)id && cancel_active_canvas_interaction(doc, old_tool)) {
        invalidate_window(doc->canvas_win);
      }
      g_app->current_tool = id;
      int group = imageeditor_tool_group(id);
      if (group == ID_TOOL_BRUSH) g_app->brush_tool = id;
      if (group == ID_TOOL_RECT) g_app->shape_tool = id;
      IE_TRACE("tool switch old=%d tool=%d group=%d", old_tool, id, group);
      IE_DEBUG("tool_switch doc=%p %s -> %s",
               (void *)doc,
               tool_id_name(old_tool),
               tool_id_name((int)id));
      if (g_app->tool_win)
        send_message(g_app->tool_win, tbSetActiveButton, (uint32_t)group, NULL);
      imageeditor_sync_tool_options();
      break;
    }

    case ID_WINDOW_TOOLS:
      if (g_app->tool_win) {
        show_window(g_app->tool_win, true);
      } else {
        window_t *tp = create_tool_palette_window();
        send_message(tp, tbSetActiveButton, (uint32_t)imageeditor_tool_group(g_app->current_tool), NULL);
      }
      break;

#if !IMAGEEDITOR_BW
    case ID_WINDOW_COLORS:
      if (g_app->color_win) {
        show_window(g_app->color_win, true);
      } else {
        create_color_palette_window();
      }
      break;

    case ID_WINDOW_LAYERS:
      if (g_app->layers_win) {
        show_window(g_app->layers_win, true);
      } else {
        create_layers_window();
      }
      break;
#endif

    case ID_LAYER_NEW:
      if (doc) {
        uint32_t fill;
        if (show_new_layer_dialog(doc->canvas_win, &fill))
          cmd_layer_new(doc, fill);
      }
      break;

    case ID_LAYER_DUPLICATE:
      cmd_layer_duplicate(doc);
      break;

    case ID_LAYER_DELETE:
      cmd_layer_delete(doc);
      break;

    case ID_LAYER_MOVE_UP:
      cmd_layer_move_up(doc);
      break;

    case ID_LAYER_MOVE_DOWN:
      cmd_layer_move_down(doc);
      break;

    case ID_LAYER_MERGE_DOWN:
      cmd_layer_merge_down(doc);
      break;

    case ID_LAYER_FLATTEN:
      cmd_layer_flatten(doc);
      break;

    case ID_LAYER_FILL_BACKGROUND:
      if (doc && g_app)
        cmd_layer_fill(doc, g_app->bg_color);
      break;

    case ID_LAYER_FILL_FOREGROUND:
      if (doc && g_app)
        cmd_layer_fill(doc, g_app->fg_color);
      break;

    case ID_LAYER_ADD_MASK:
      if (doc) {
        if (!ie_doc_begin_op(doc, "Add Mask")) break;
        int fill_mode = MASK_EXTRACT_WHITE;
        if (show_add_mask_dialog(doc->win ? doc->win : g_app->menubar_win, &fill_mode)) {
          if (!layer_add_mask_ex(doc, doc->layer.active, fill_mode)) {
            ie_doc_commit_op(doc, false);
            break;
          }
          doc->layer.editing_mask = true;
          if (doc->canvas_win) {
            canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
            if (state) {
              canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                                       state->hover_valid);
            }
          }
        } else {
          ie_doc_commit_op(doc, false);
          break;
        }
        ie_doc_commit_op(doc, true);
      }
      break;

    case ID_LAYER_APPLY_MASK:  cmd_layer_apply_mask(doc);  break;
    case ID_LAYER_REMOVE_MASK: cmd_layer_remove_mask(doc); break;

    case ID_LAYER_EXTRACT_MASK:
      if (doc) canvas_extract_mask(doc);
      break;

    case ID_COLOR_SWAP:
      swap_foreground_background_colors();
      break;

    case ID_LAYER_EDIT_MASK:
      if (doc && doc->layer.count > 0) {
        doc->layer.editing_mask = !doc->layer.editing_mask;
        layers_win_refresh();
        if (doc->canvas_win) {
          canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
          if (state) {
            canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                                     state->hover_valid);
          }
        }
        if (doc->canvas_win) invalidate_window(doc->canvas_win);
      }
      break;

    case ID_ANIM_NEW_FRAME:       cmd_frame_add(doc, false); break;
    case ID_ANIM_DUPLICATE_FRAME: cmd_frame_add(doc, true);  break;
    case ID_ANIM_DELETE_FRAME:    cmd_frame_delete(doc);     break;

    case ID_ANIM_PREV_FRAME:
      if (doc)
        anim_step_frame(doc, -1);
      break;

    case ID_ANIM_NEXT_FRAME:
      if (doc)
        anim_step_frame(doc, +1);
      break;

    case ID_ANIM_PLAY:
      if (doc && doc->anim && !doc->anim->playing) {
        // Ensure the timeline window exists; recreate it if it was closed.
        if (g_app && !g_app->timeline_win)
          create_timeline_window();
        if (!g_app || !g_app->timeline_win) break;
        // Start a repeating timer; interval = frame period at current FPS.
        // Default to 12 fps (≈83 ms) if fps is 0 or unset.
        static const uint32_t kDefaultFrameIntervalMs = 83u; // ≈12 fps
        uint32_t interval = (doc->anim->fps > 0)
                            ? (uint32_t)(1000 / doc->anim->fps)
                            : kDefaultFrameIntervalMs;
        int delay = doc->anim->frames[doc->anim->active_frame]->delay_ms;
        if (delay > 0) interval = (uint32_t)delay;
        if (g_app->anim_timer_id)
          axCancelTimer(g_app->anim_timer_id);
        g_app->anim_timer_id = axSetTimer(
            g_app->timeline_win, interval, NULL, (bool_t)1);
        if (g_app->anim_timer_id) {
          anim_frame_compress(doc->anim->frames[doc->anim->active_frame],
                              doc->pixels, doc->canvas_w, doc->canvas_h,
                              IE_FRAME_FORMAT);
          doc->anim->playback_start_frame = doc->anim->active_frame;
          doc->anim->playing = true;
          IE_TRACE("playback start win=%p frame=%d count=%d", (void *)doc->canvas_win,
                   doc->anim->active_frame, doc->anim->frame_count);
          if (doc->canvas_win) invalidate_window(doc->canvas_win);
          timeline_win_refresh();
        }
      }
      break;

    case ID_ANIM_STOP:
      if (doc && doc->anim) {
        anim_stop_playback(doc);
        timeline_win_refresh();
      }
      break;

    case ID_ANIM_LOOP:
      if (doc && doc->anim) {
        anim_stop_playback(doc);
        if (!ie_doc_begin_op(doc, "Loop Playback")) break;
        doc->anim->loop = !doc->anim->loop;
        ie_doc_commit_op(doc, true);
      }
      break;

    case ID_ANIM_TRACE:
      if (g_app) {
        g_app->anim_trace_enabled = !g_app->anim_trace_enabled;
        if (doc && doc->canvas_win)
          invalidate_window(doc->canvas_win);
        timeline_win_refresh();
      }
      break;

    case ID_ANIM_ONION_SKIN:
      if (show_onion_skin_dialog(doc && doc->win ? doc->win : g_app->menubar_win)) {
        if (doc && doc->canvas_win)
          invalidate_window(doc->canvas_win);
        timeline_win_refresh();
      }
      break;

    case ID_ANIM_EXPORT_GIF: {
      if (!doc || !doc->anim) break;
      char path[512] = "Untitled.gif";
      if (show_file_picker(g_app->menubar_win, true, path, sizeof(path))) {
        if (anim_export_gif(doc, path))
          send_message(doc->win, evStatusBar, 0, (void *)"GIF exported");
        else
          send_message(doc->win, evStatusBar, 0, (void *)"GIF export failed");
      }
      break;
    }

    case ID_ANIM_EXPORT_APNG: {
      if (!doc || !doc->anim) break;
      char path[512] = "Untitled.png";
      if (show_file_picker(g_app->menubar_win, true, path, sizeof(path))) {
        if (anim_export_apng(doc, path))
          send_message(doc->win, evStatusBar, 0, (void *)"APNG exported");
        else
          send_message(doc->win, evStatusBar, 0, (void *)"APNG export failed");
      }
      break;
    }

    case ID_ANIM_EXPORT_SPRITESHEET: {
      if (!doc || !doc->anim) break;
      char path[512] = "Untitled.png";
      if (show_file_picker(g_app->menubar_win, true, path, sizeof(path))) {
        if (anim_export_spritesheet(doc, path))
          send_message(doc->win, evStatusBar, 0, (void *)"Sprite sheet exported");
        else
          send_message(doc->win, evStatusBar, 0, (void *)"Sprite sheet export failed");
      }
      break;
    }

    case ID_WINDOW_TIMELINE:
      IE_TRACE("frames toggle win=%p", (void *)g_app->timeline_win);
      if (g_app->timeline_win) {
        show_window(g_app->timeline_win, !window_has_state(g_app->timeline_win, WINDOW_STATE_VISIBLE));
      } else {
        create_timeline_window();
      }
      break;

    default:
#if !IMAGEEDITOR_INDEXED
      if (id >= ID_FILTER_BASE && id < ID_FILTER_BASE + IMAGEEDITOR_MAX_FILTERS) {
        if (doc) {
          int filter_idx = (int)id - ID_FILTER_BASE;
          if (filter_idx >= 0 && filter_idx < g_app->filter_count) {
            if (!ie_doc_begin_op(doc, "Photo Filter")) break;
            ie_doc_commit_op(doc, imageeditor_apply_filter(doc, filter_idx));
            doc_update_title(doc);
            if (doc->canvas_win)
              invalidate_window(doc->canvas_win);
            if (doc->win)
              send_message(doc->win, evStatusBar, 0,
                           (void *)g_app->filters[filter_idx].name);
          }
        }
        break;
      }
#endif // !IMAGEEDITOR_INDEXED

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

void imageeditor_sync_filter_menu(void) {
#if !IMAGEEDITOR_INDEXED
  if (!g_app) return;

  int n = 0;
  for (int i = 0; i < FILTER_PREFIX_COUNT; i++)
    s_filter_items[n++] = MENU_FILTER_ITEMS[i];

  s_filter_photo_item_count = 0;
  for (int i = 0; i < g_app->filter_count &&
                  i < (int)(sizeof(s_filter_photo_items) / sizeof(s_filter_photo_items[0]));
       i++) {
    snprintf(s_filter_photo_labels[i], sizeof(s_filter_photo_labels[i]),
             "%s", g_app->filters[i].name);
    s_filter_photo_items[s_filter_photo_item_count++] = (menu_item_t){
      s_filter_photo_labels[i],
      (uint16_t)(ID_FILTER_BASE + i),
      NULL,
      0
    };
  }

  for (int i = 0; i < n; i++) {
    if (s_filter_items[i].label &&
        strcmp(s_filter_items[i].label, "Photo") == 0) {
      s_filter_items[i].submenu_items = s_filter_photo_items;
      s_filter_items[i].submenu_count = s_filter_photo_item_count;
      break;
    }
  }

  s_filter_item_count = n;
  menu_def_t *filter_menu = find_menu("Filter");
  if (filter_menu) {
    filter_menu->items = s_filter_items;
    filter_menu->item_count = s_filter_item_count;
  }
  publish_dynamic_menus();
#endif // !IMAGEEDITOR_INDEXED
}

result_t editor_menubar_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  if (msg == evCommand) {
    uint16_t notif = HIWORD(wparam);
    if (notif == kMenuBarNotificationItemClick ||
        notif == kAcceleratorNotification      ||
        notif == btnClicked) {
      handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  // Rebuild dynamic menus just before a popup opens so they always reflect
  // the current state (open documents, grid toggle states, etc.).
  if (msg == evLeftButtonDown) {
    window_menu_rebuild();
    view_menu_rebuild();
  }
  return win_menubar(win, msg, wparam, lparam);
}
