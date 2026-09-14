#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <orion/ui.h>
#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "commctl.h"

// Forward declare list control procedure  
extern result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Helper functions (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);
extern int titlebar_height(window_t const *win);
extern void show_window(window_t *win, bool visible);

// Open the dropdown list popup for 'win' (combobox).
static void open_dropdown(window_t *win) {
  if (!win)
    return;

  combobox_state_t *state = (combobox_state_t *)win->userdata;
  if (!state || !state->texts || win->cursor_pos == 0)
    return;

  // Determine the screen-absolute position of the combobox bottom edge.
  // Toolbar children have toolbar-band-relative frame.x/y; regular body
  // children have root-client-relative frames.
  int abs_x, abs_y;
  bool is_toolbar_child = false;
  if (win->parent) {
    toolbar_state_t *tb = window_toolbar_state(win->parent);
    for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next) {
      if (tc == win) { is_toolbar_child = true; break; }
    }
  }
  if (is_toolbar_child) {
    window_t *parent = win->parent;
    int parent_title_h = (parent->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
    abs_x = parent->frame.x + win->frame.x;
    abs_y = parent->frame.y + parent_title_h + win->frame.y + win->frame.h + 2;
  } else {
    // Walk parent chain to compute absolute screen position
    abs_x = win->frame.x;
    abs_y = win->frame.y + win->frame.h + 2;
    
    for (window_t *p = win->parent; p; p = p->parent) {
      int title_h = (p->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
      abs_x += p->frame.x;
      abs_y += p->frame.y + title_h;
    }
  }
  int visible_items = MIN((int)win->cursor_pos, COMBOBOX_DROPDOWN_MAX_VISIBLE);
  irect16_t rect = {abs_x, abs_y, win->frame.w, visible_items * (FONT_SIZE_SMALL + 5)};
  window_t *list = create_window("", WINDOW_NOTITLE|WINDOW_NORESIZE|WINDOW_VSCROLL|WINDOW_ALWAYSONTOP|WINDOW_NOTRAYBUTTON, &rect, NULL, win_list, win->hinstance, NULL);
  if (!list)
    return;
  list->userdata = win;
  list->userdata2 = malloc(sizeof(win->title));
  if (list->userdata2)
    memcpy(list->userdata2, win->title, sizeof(win->title));

  result_t sel = send_message(win, cbGetCurrentSelection, 0, NULL);
  if (sel != (result_t)kComboBoxError)
    send_message(list, lstSetItem, (uint32_t)sel, NULL);
  show_window(list, true);
  set_capture(list);
  set_focus(list);
}

// Helper: Populate combobox from database using combobox_params_t
static void cb_populate_from_database(window_t *win, const combobox_params_t *params) {
  if (!params || !params->display_field || !params->value_field) {
    return;
  }
  
  // If db is NULL, try to get it from window (set via evSetDatabase)
  database_t *db = params->db;
  if (!db) {
    // Database pointer not set - will need to be set via message later
    return;
  }
  
  // Fetch all records from source table
  result_node_t *results = (result_node_t *)send_db_message(db, dbFetch,
    MAKEDWORD(params->table_id, 0), (void *)0);
  
  if (!results) {
    return;
  }
  
  // Get object proc and field bindings for this table
  db_object_proc_t obj_proc = (db_object_proc_t)send_db_message(db, dbGetObjectProc,
    (uint32_t)params->table_id, NULL);
  
  int binding_count = 0;
  const db_field_msg_binding_t *bindings = (const db_field_msg_binding_t *)send_db_message(
    db, dbGetFieldBindings, (uint32_t)params->table_id, &binding_count);
  
  if (!obj_proc || !bindings) {
    free_result_list(results);
    return;
  }
  
  combobox_state_t *state = (combobox_state_t *)win->userdata;
  if (!state) {
    free_result_list(results);
    return;
  }
  
  // Allocate values array if not already allocated
  if (!state->values) {
    state->values = calloc(MAX_COMBOBOX_STRINGS, sizeof(int));
  }
  
  // Add each record to combobox
  char display_buf[256];
  int count = 0;
  for (result_node_t *node = results; node && count < MAX_COMBOBOX_STRINGS; node = node->next) {
    display_buf[0] = '\0';
    
    // Extract display field text using DDX-style field interrogation
    // CRITICAL: dbFetch stores pointers in node->data (db_author_t **), not structs.
    // Example: *(db_author_t **)node->data = &ctx->authors[i];
    // So we must dereference to get the actual record pointer.
    void *record = *(void **)node->data;
    bool success = db_object_get_field_text(bindings, binding_count, obj_proc,
                                  record, params->display_field,
                                  display_buf, sizeof(display_buf));
    
    if (success && display_buf[0] != '\0') {
      if (params->filter_field && params->filter_field[0] &&
          params->filter_value && params->filter_value[0]) {
        char filter_buf[64] = {0};
        if (!db_object_get_field_text(bindings, binding_count, obj_proc,
                                      record, params->filter_field,
                                      filter_buf, sizeof(filter_buf)) ||
            strcmp(filter_buf, params->filter_value) != 0)
          continue;
      }
      send_message(win, cbAddString, 0, display_buf);
      char value_buf[64];
      if (db_object_get_field_text(bindings, binding_count, obj_proc,
                                    record, params->value_field,
                                    value_buf, sizeof(value_buf))) {
        state->values[count] = atoi(value_buf);
      }
      count++;
    }
  }
  
  free_result_list(results);
}

// Combobox control window procedure
result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  combobox_state_t *state = (combobox_state_t *)win->userdata;
  combobox_string_t *texts = state ? state->texts : NULL;
  
  switch (msg) {
    case evCreate: {
      control_apply_predefined_height(win, "combobox");
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+16);
      
      // Allocate state
      state = (combobox_state_t *)calloc(1, sizeof(combobox_state_t));
      if (!state) return false;
      state->texts = (combobox_string_t *)malloc(sizeof(combobox_string_t) * MAX_COMBOBOX_STRINGS);
      if (!state->texts) {
        free(state);
        return false;
      }
      win->userdata = state;
      texts = state->texts;
      
      // Check if created from form with combobox_params_t
      if ((uintptr_t)lparam > 0x1000) {
        form_ctrl_def_t *cd = (form_ctrl_def_t *)lparam;
        if (cd->lparam) {
          combobox_params_t *params = (combobox_params_t *)cd->lparam;
          // Copy params to state
          state->params = *params;
          
          // Try to get database if params has db name info.
          // When unavailable at create time, the parent later propagates
          // evSetDatabase through the child tree.
          cb_populate_from_database(win, &state->params);
        }
      }
      
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        int text_w = strwidth(win->title) + 16 + 16; /* text + padding + arrow */
        m->desired_w = MAX(win->frame.w > 0 ? win->frame.w : 60, text_w);
        m->desired_h = control_predefined_height(win->flags);
      }
      return true;
    }
    case evArrange:
      return control_arrange_predefined_height(win, (layout_arrange_t *)lparam);
    case evDestroy:
      if (state) {
        free(state->texts);
        free(state->values);
        free(state);
      }
      return true;
    case evPaint:
      {
        irect16_t local = {0, 0, win->frame.w, win->frame.h};
        irect16_t arrow = rect_split_right(local, MIN(win->frame.h, 16));
        irect16_t text_rect = {2, 0, arrow.x - 4, win->frame.h};
        bool show_pressed = window_has_state(win, WINDOW_STATE_PRESSED) ||
                            ((win->flags & BUTTON_PUSHLIKE) && win->value);
        ctrl_state_t state = CTRL_NORMAL;
        if (show_pressed)                                 state |= CTRL_PRESSED;
        if (window_has_state(win, WINDOW_STATE_HOVERED))  state |= CTRL_HOVER;
        if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
        if (g_ui_runtime.focused == win)                  state |= CTRL_FOCUSED;
        get_theme()->draw_combobox_bg(local, state);
        bool disabled = (state & CTRL_DISABLED) != 0;
        uint32_t text_col = disabled ? get_sys_color(brTextDisabled) : get_sys_color(brTextNormal);
        draw_text_clipped(FONT_SYSTEM, win->title, &text_rect, text_col, TEXT_PADDING_LEFT);
        draw_theme_icon_in_rect(THEME_ICON_ARROW_UPDOWN, arrow, text_col);
      }
      return true;
    case evLeftButtonUp:
      // Do not forward button-up to win_button() to avoid sending btnClicked
      // to the parent when opening a dropdown.
      window_set_state(win, WINDOW_STATE_PRESSED, false);
      invalidate_window(win);
      open_dropdown(win);
      return true;
    case evKeyDown: {
      uint32_t key = wparam;
      if (key == AX_KEY_SPACE || key == AX_KEY_ENTER || key == AX_KEY_KP_ENTER) {
        open_dropdown(win);
        return true;
      }
      if (key == AX_KEY_UPARROW) {
        result_t sel = send_message(win, cbGetCurrentSelection, 0, NULL);
        if (sel != (result_t)kComboBoxError && sel > 0) {
          send_message(win, cbSetCurrentSelection, (uint32_t)(sel - 1), NULL);
          invalidate_window(win);
          send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, cbSelectionChange), win);
        }
        return true;
      }
      if (key == AX_KEY_DOWNARROW) {
        result_t sel = send_message(win, cbGetCurrentSelection, 0, NULL);
        if (sel == (result_t)kComboBoxError && win->cursor_pos > 0) {
          send_message(win, cbSetCurrentSelection, 0, NULL);
          invalidate_window(win);
          send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, cbSelectionChange), win);
        } else if (sel != (result_t)kComboBoxError && (uint32_t)(sel + 1) < win->cursor_pos) {
          send_message(win, cbSetCurrentSelection, (uint32_t)(sel + 1), NULL);
          invalidate_window(win);
          send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, cbSelectionChange), win);
        }
        return true;
      }
      return false;
    }
    case evKeyUp:
      /* Consume Space/Enter key-up to prevent win_button's handler from
         sending a spurious btnClicked to the parent. */
      if (wparam == AX_KEY_SPACE || wparam == AX_KEY_ENTER || wparam == AX_KEY_KP_ENTER)
        return true;
      return win_button(win, msg, wparam, lparam);
    case evMouseMove:
      track_mouse(win);
      if (!window_has_state(win, WINDOW_STATE_HOVERED)) {
        window_set_state(win, WINDOW_STATE_HOVERED, true);
        invalidate_window(win);
      }
      return false;
    case evMouseLeave:
      window_set_state(win, WINDOW_STATE_HOVERED, false);
      invalidate_window(win);
      return false;
    case cbClear:
      memset(texts, 0, sizeof(combobox_string_t) * win->cursor_pos);
      win->cursor_pos = 0;
      win->title[0] = '\0';
      return true;
    case cbAddString:
      if (win->cursor_pos < MAX_COMBOBOX_STRINGS) {
        strncpy(texts[win->cursor_pos++], lparam, sizeof(combobox_string_t));
        strncpy(win->title, lparam, sizeof(win->title));
        win->title[sizeof(win->title) - 1] = '\0';
        return true;
      } else {
        return false;
      }
    case cbGetListBoxText:
      if (wparam < win->cursor_pos) {
        strcpy(lparam, texts[wparam]);
        return true;
      } else {
        return false;
      }
    case cbSetCurrentSelection:
      if (wparam < win->cursor_pos) {
        strncpy(win->title, texts[wparam], sizeof(win->title));
        win->title[sizeof(win->title) - 1] = '\0';
        return true;
      } else {
        return false;
      }
    case cbGetCurrentSelection:
      for (uint32_t i = 0; i < win->cursor_pos; i++) {
        if (!strncmp(texts[i], win->title, sizeof(win->title))) {
          if (lparam)
            *(int *)lparam = (int)i;
          return i;
        }
      }
      if (lparam)
        *(int *)lparam = kComboBoxError;
      return kComboBoxError;
    case cbGetCurrentValue:
      // Return value_field data (e.g., ID) instead of row index
      if (state && state->values) {
        for (uint32_t i = 0; i < win->cursor_pos; i++) {
          if (!strncmp(texts[i], win->title, sizeof(win->title))) {
            if (lparam)
              *(int *)lparam = state->values[i];
            return state->values[i];
          }
        }
      }
      if (lparam)
        *(int *)lparam = kComboBoxError;
      return kComboBoxError;
    case evSetDatabase:
      // Set database pointer and populate combobox
      if (state && lparam) {
        state->params.db = (database_t *)lparam;
        // Clear existing items
        send_message(win, cbClear, 0, NULL);
        // Populate from database
        cb_populate_from_database(win, &state->params);
        invalidate_window(win);
      }
      return true;
    default:
      return win_button(win, msg, wparam, lparam);
  }
}
