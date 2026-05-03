// Menu bar window proc and menu command handler

#include "imageeditor.h"

#define FILTER_PREFIX_COUNT ((int)(sizeof(kFilterItems) / sizeof(kFilterItems[0])))
#define WINDOW_PREFIX_COUNT ((int)(sizeof(kWindowItems) / sizeof(kWindowItems[0])))

static menu_item_t s_filter_items[FILTER_PREFIX_COUNT + 1 + IMAGEEDITOR_MAX_FILTERS];
static int         s_filter_item_count = FILTER_PREFIX_COUNT;
static menu_item_t s_filter_photo_items[IMAGEEDITOR_MAX_FILTERS];
static int         s_filter_photo_item_count = 0;
static char        s_filter_photo_labels[IMAGEEDITOR_MAX_FILTERS][64];

// Persistent storage for dynamically built items and document title strings.
static menu_item_t s_window_items[WINDOW_PREFIX_COUNT + WINDOW_MENU_MAX_DOCS];
static int         s_window_item_count = WINDOW_PREFIX_COUNT;

static bool cancel_active_canvas_interaction(canvas_doc_t *doc, int old_tool) {
  bool changed = false;
  canvas_win_state_t *state;

  if (!doc) return false;

  state = doc->canvas_win ? (canvas_win_state_t *)doc->canvas_win->userdata : NULL;
  if (state && state->panning) {
    IE_DEBUG("cancel_interaction pan doc=%p old_tool=%s",
             (void *)doc, tool_id_name(old_tool));
    state->panning = false;
    changed = true;
  }

  if (old_tool == ID_TOOL_POLYGON && doc->poly_active) {
    IE_DEBUG("cancel_interaction polygon doc=%p points=%d",
             (void *)doc, doc->poly_count);
    if (doc->shape_snapshot) {
      memcpy(doc->pixels, doc->shape_snapshot,
             (size_t)doc->canvas_w * doc->canvas_h * 4);
      doc->canvas_dirty = true;
    }
    doc_discard_undo(doc);
    doc->poly_active = false;
    doc->poly_count = 0;
    changed = true;
  }

  if (canvas_is_shape_tool(old_tool) && doc->drawing && doc->shape_snapshot) {
    IE_DEBUG("cancel_interaction shape doc=%p tool=%s start=(%d,%d) last=(%d,%d)",
             (void *)doc, tool_id_name(old_tool),
             doc->shape_start.x, doc->shape_start.y,
             doc->last.x, doc->last.y);
    memcpy(doc->pixels, doc->shape_snapshot,
           (size_t)doc->canvas_w * doc->canvas_h * 4);
    doc->canvas_dirty = true;
    changed = true;
  }

  if (doc->sel_moving) {
    IE_DEBUG("cancel_interaction selection_move doc=%p float_pos=(%d,%d)",
             (void *)doc, doc->float_pos.x, doc->float_pos.y);
    canvas_commit_move(doc);
    changed = true;
  }

  if (doc->sel_mask_moving) {
    IE_DEBUG("cancel_interaction selection_mask_move doc=%p", (void *)doc);
    doc->sel_mask_moving = false;
    changed = true;
  }

  if (old_tool == ID_TOOL_CROP && doc->sel_active) {
    IE_DEBUG("cancel_interaction crop doc=%p", (void *)doc);
    canvas_deselect(doc);
    changed = true;
  }

  if (doc->drawing) {
    IE_DEBUG("cancel_interaction drawing doc=%p old_tool=%s",
             (void *)doc, tool_id_name(old_tool));
    doc->drawing = false;
    changed = true;
  }

  return changed;
}

// ============================================================
// Palette window helpers (shared by gem_init / handle_menu_command)
// ============================================================

window_t *create_tool_palette_window(void) {
  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, PALETTE_WIN_Y, PALETTE_WIN_W, TOOL_WIN_H),
      NULL, win_tool_palette_proc, g_app->hinstance, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;
  return tp;
}

window_t *create_tool_options_window(void) {
  window_t *tw = create_window(
      "Options",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(TOOL_OPTIONS_WIN_X, TOOL_OPTIONS_WIN_Y,
               TOOL_OPTIONS_WIN_W, TOOL_OPTIONS_WIN_H),
      NULL, win_tool_options_proc, g_app->hinstance, NULL);
  show_window(tw, true);
  g_app->tool_options_win = tw;
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

static result_t main_toolbar_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      send_message(win, tbSetItems,
                   (uint32_t)kMainToolbarCount,
                   (void *)kMainToolbar);
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
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  window_t *win = create_window(
      "Toolbar",
      WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_ALWAYSONTOP |
      WINDOW_NORESIZE | WINDOW_NOTRAYBUTTON,
      MAKERECT(0, APP_TOOLBAR_Y, sw, APP_TOOLBAR_H),
      NULL, main_toolbar_proc,
      g_app->hinstance, NULL);
  if (!win) return NULL;
  show_window(win, true);
  g_app->main_toolbar_win = win;
  imageeditor_sync_main_toolbar();
  return win;
}

void imageeditor_sync_main_toolbar(void) {
  if (!g_app || !g_app->main_toolbar_win) return;
  window_t *btn = get_window_item(g_app->main_toolbar_win, ID_VIEW_MASK_ONLY);
  if (!btn) return;
  bool checked = g_app->active_doc && g_app->active_doc->mask_only_view;
  send_message(btn, btnSetCheck, checked ? btnStateChecked : btnStateUnchecked, NULL);
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
  int n = (int)(sizeof(s_view_items) / sizeof(s_view_items[0]));
  for (int i = 0; i < n; i++) {
    if (s_view_items[i].id == ID_VIEW_SHOW_GRID)
      s_view_items[i].label = g_app->grid_visible
                              ? MENU_CHECK_ON "Show Grid"
                              : MENU_CHECK_OFF "Show Grid";
    if (s_view_items[i].id == ID_VIEW_SNAP_GRID)
      s_view_items[i].label = g_app->grid_snap
                              ? MENU_CHECK_ON "Snap to Grid"
                              : MENU_CHECK_OFF "Snap to Grid";
    if (s_view_items[i].id == ID_VIEW_MASK_ONLY)
      s_view_items[i].label = (doc && doc->mask_only_view)
                              ? MENU_CHECK_ON "Mask Only View"
                              : MENU_CHECK_OFF "Mask Only View";
  }
}

void window_menu_rebuild(void) {
  if (!g_app) return;

  int n = 0;
  for (int i = 0; i < WINDOW_PREFIX_COUNT; i++)
    s_window_items[n++] = kWindowItems[i];

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
  kMenus[MENU_WINDOW_INDEX].items      = s_window_items;
  kMenus[MENU_WINDOW_INDEX].item_count = s_window_item_count;

  // In standalone mode push the updated menus to our local menubar window.
  // In gem mode menubar_win is NULL; the shell holds a pointer to kMenus
  // (set during gem_init) and will read the updated data on its next redraw.
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetMenus,
                 (uint32_t)kNumMenus, kMenus);
}

bool imageeditor_open_file_path(const char *path) {
  if (!g_app || !path || !path[0]) return false;

  int img_w = 0, img_h = 0;
  uint8_t *px = load_image(path, &img_w, &img_h);
  if (!px || img_w <= 0 || img_h <= 0) {
    if (px) image_free(px);
    return false;
  }

  canvas_doc_t *ndoc = create_document(path, img_w, img_h);
  if (!ndoc) {
    image_free(px);
    return false;
  }

  // Swap the white placeholder pixels for the actual loaded image.
  // Update both the layer buffer and the convenience alias.
  image_free(ndoc->layers[0]->pixels);
  ndoc->layers[0]->pixels = px;
  ndoc->pixels = px;
  ndoc->canvas_dirty = true;
  ndoc->modified = false;
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
  return true;
}

void handle_menu_command(uint16_t id) {
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
        imageeditor_open_file_path(path);
      }
      break;
    }

    case ID_FILE_SAVE:
      if (!doc) break;
      if (!doc->filename[0]) goto do_save_as;
      if (png_save(doc->filename, doc)) {
        doc->modified = false;
        doc_update_title(doc);
        send_message(doc->win, evStatusBar, 0, (void *)"Saved");
      } else {
        send_message(doc->win, evStatusBar, 0, (void *)"Save failed");
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
          send_message(doc->win, evStatusBar, 0, path);
        } else {
          send_message(doc->win, evStatusBar, 0, (void *)"Save failed");
        }
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
        canvas_cut_selection(doc, MAKE_COLOR(0, 0, 0, 0));
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

    case ID_SELECT_CLEAR:
      if (doc && doc->sel_active) {
        doc_push_undo(doc);
        canvas_clear_selection(doc, MAKE_COLOR(0, 0, 0, 0));
        doc_update_title(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_SELECT_ALL:
      if (doc) {
        canvas_select_all(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_SELECT_DESELECT:
      if (doc) {
        canvas_deselect(doc);
        invalidate_window(doc->canvas_win);
      }
      break;

    case ID_SELECT_EXPAND:
      if (doc && doc->sel_active) {
        int amount = 1;
        if (show_selection_modify_dialog(doc->win ? doc->win : g_app->menubar_win,
                                         "Expand Selection", &amount) &&
            canvas_expand_selection(doc, amount)) {
          invalidate_window(doc->canvas_win);
        }
      }
      break;

    case ID_SELECT_CONTRACT:
      if (doc && doc->sel_active) {
        int amount = 1;
        if (show_selection_modify_dialog(doc->win ? doc->win : g_app->menubar_win,
                                         "Contract Selection", &amount) &&
            canvas_contract_selection(doc, amount)) {
          invalidate_window(doc->canvas_win);
        }
      }
      break;

    case ID_IMAGE_CROP:
      if (doc && doc->sel_active) {
        doc_push_undo(doc);
        if (canvas_crop_or_expand_to_selection(doc)) {
          doc_update_title(doc);
          invalidate_window(doc->canvas_win);
        }
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

    case ID_IMAGE_LEVELS:
      if (doc && doc->active_layer >= 0 && doc->active_layer < doc->layer_count) {
        doc_push_undo(doc);
        if (!show_levels_dialog(doc->win ? doc->win : g_app->menubar_win))
          doc_discard_undo(doc);
      }
      break;

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
    case ID_FILTER_SHARPEN:
    case ID_FILTER_EDGE:
      if (doc) {
        doc_push_undo(doc);
        if (!imageeditor_apply_builtin_filter(doc, id)) {
          doc_discard_undo(doc);
          break;
        }
        doc_update_title(doc);
        if (doc->canvas_win)
          invalidate_window(doc->canvas_win);
      }
      break;

    case ID_IMAGE_RESIZE: {
      if (!doc) break;
      int new_w = doc->canvas_w, new_h = doc->canvas_h;
      if (show_size_dialog(g_app->menubar_win, "Canvas Size", &new_w, &new_h) &&
          (new_w != doc->canvas_w || new_h != doc->canvas_h)) {
        doc_push_undo(doc);
        if (canvas_resize(doc, new_w, new_h)) {
          canvas_deselect(doc);
          if (doc->canvas_win) {
            canvas_win_sync_scrollbars(doc->canvas_win);
            invalidate_window(doc->canvas_win);
          }
          doc_update_title(doc);
          char sb[32];
          snprintf(sb, sizeof(sb), "%dx%d", doc->canvas_w, doc->canvas_h);
          send_message(doc->win, evStatusBar, 0, sb);
        }
      }
      break;
    }

    case ID_HELP_ABOUT:
      show_about_dialog(g_app->menubar_win);
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
      g_app->grid_visible = !g_app->grid_visible;
      if (doc && doc->canvas_win) invalidate_window(doc->canvas_win);
      break;

    case ID_VIEW_SNAP_GRID:
      g_app->grid_snap = !g_app->grid_snap;
      break;

    case ID_VIEW_GRID_OPTIONS: {
      int gx = g_app->grid_spacing_x > 0 ? g_app->grid_spacing_x : 8;
      int gy = g_app->grid_spacing_y > 0 ? g_app->grid_spacing_y : 8;
      if (show_grid_options_dialog(g_app->menubar_win, &gx, &gy)) {
        g_app->grid_spacing_x = gx;
        g_app->grid_spacing_y = gy;
        if (doc && doc->canvas_win && g_app->grid_visible)
          invalidate_window(doc->canvas_win);
      }
      break;
    }

    case ID_VIEW_MASK_ONLY:
      if (doc) {
        doc_set_mask_only_view(doc, !doc->mask_only_view);
        if (doc->canvas_win)
          invalidate_window(doc->canvas_win);
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
      IE_DEBUG("tool_switch doc=%p %s -> %s",
               (void *)doc,
               tool_id_name(old_tool),
               tool_id_name((int)id));
      if (g_app->tool_win)
        send_message(g_app->tool_win, bxSetActiveItem, (uint32_t)id, NULL);
      if (g_app->tool_options_win)
        invalidate_window(g_app->tool_options_win);
      break;
    }

    case ID_WINDOW_TOOLS:
      if (g_app->tool_win) {
        show_window(g_app->tool_win, true);
      } else {
        window_t *tp = create_tool_palette_window();
        send_message(tp, bxSetActiveItem, (uint32_t)g_app->current_tool, NULL);
      }
      break;

    case ID_WINDOW_COLORS:
      if (g_app->color_win) {
        show_window(g_app->color_win, true);
      } else {
        create_color_palette_window();
      }
      break;

#if !IMAGEEDITOR_SINGLE_LAYER
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
        if (!show_new_layer_dialog(doc->canvas_win, &fill)) break;
        doc_push_undo(doc);
        if (!doc_add_layer_filled(doc, fill)) { doc_discard_undo(doc); break; }
        doc->canvas_dirty = true;
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_DUPLICATE:
      if (doc) {
        doc_push_undo(doc);
        if (!doc_duplicate_layer(doc)) { doc_discard_undo(doc); break; }
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_DELETE:
      if (doc) {
        doc_push_undo(doc);
        if (!doc_delete_layer(doc)) { doc_discard_undo(doc); break; }
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_MOVE_UP:
      if (doc) {
        doc_push_undo(doc);
        doc_move_layer_up(doc);
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_MOVE_DOWN:
      if (doc) {
        doc_push_undo(doc);
        doc_move_layer_down(doc);
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_MERGE_DOWN:
      if (doc) {
        doc_push_undo(doc);
        doc_merge_down(doc);
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_FLATTEN:
      if (doc) {
        doc_push_undo(doc);
        doc_flatten(doc);
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_ADD_MASK:
      if (doc) {
        doc_push_undo(doc);
        int fill_mode = MASK_EXTRACT_WHITE;
        if (show_add_mask_dialog(doc->win ? doc->win : g_app->menubar_win, &fill_mode)) {
          if (!layer_add_mask_ex(doc, doc->active_layer, fill_mode)) {
            doc_discard_undo(doc);
            break;
          }
          doc->editing_mask = true;
          if (doc->canvas_win) {
            canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
            if (state) {
              canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                                       state->hover_valid);
            }
          }
        } else {
          doc_discard_undo(doc);
          break;
        }
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_APPLY_MASK:
      if (doc) {
        doc_push_undo(doc);
        layer_apply_mask(doc, doc->active_layer);
        if (doc->canvas_win) {
          canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
          if (state) {
            canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                                     state->hover_valid);
          }
        }
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_REMOVE_MASK:
      if (doc) {
        doc_push_undo(doc);
        layer_remove_mask(doc, doc->active_layer);
        if (doc->canvas_win) {
          canvas_win_state_t *state = (canvas_win_state_t *)doc->canvas_win->userdata;
          if (state) {
            canvas_win_update_status(doc->canvas_win, state->hover.x, state->hover.y,
                                     state->hover_valid);
          }
        }
        invalidate_window(doc->canvas_win);
        layers_win_refresh();
      }
      break;

    case ID_LAYER_EXTRACT_MASK:
      if (doc) canvas_extract_mask(doc);
      break;

    case ID_COLOR_SWAP:
      swap_foreground_background_colors();
      break;

    case ID_LAYER_EDIT_MASK:
      if (doc && doc->layer_count > 0) {
        doc->editing_mask = !doc->editing_mask;
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

    default:
      if (id >= ID_FILTER_BASE && id < ID_FILTER_BASE + IMAGEEDITOR_MAX_FILTERS) {
        if (doc) {
          int filter_idx = (int)id - ID_FILTER_BASE;
          if (filter_idx >= 0 && filter_idx < g_app->filter_count) {
            doc_push_undo(doc);
            if (!imageeditor_apply_filter(doc, filter_idx)) {
              doc_discard_undo(doc);
              break;
            }
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
  if (!g_app) return;

  int n = 0;
  for (int i = 0; i < FILTER_PREFIX_COUNT; i++)
    s_filter_items[n++] = kFilterItems[i];

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
  kMenus[MENU_FILTER_INDEX].items = s_filter_items;
  kMenus[MENU_FILTER_INDEX].item_count = s_filter_item_count;

  if (g_app->menubar_win) {
    send_message(g_app->menubar_win, kMenuBarMessageSetMenus,
                 (uint32_t)kNumMenus, kMenus);
  }
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
