// VB-style property browser for the form editor.
//
// Reuses win_reportview as a two-column property grid.  Value cells edit in
// place by overlaying a win_textedit on top of the clicked report cell.

#include "formeditor.h"
#include "../../commctl/commctl.h"

enum {
  PROP_ROW_NONE = 0,
  PROP_ROW_NAME,
  PROP_ROW_CAPTION,
  PROP_ROW_TYPE,
  PROP_ROW_ID,
  PROP_ROW_LEFT,
  PROP_ROW_TOP,
  PROP_ROW_WIDTH,
  PROP_ROW_HEIGHT,
};

typedef struct {
  window_t *list_win;
  window_t *edit_win;
  uint32_t  edit_prop_id;
  int       edit_row;
} prop_browser_state_t;

#define PROP_VALUE_X 72
#define PROP_HEADER_H 0

static result_t prop_edit_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam);

static const char *prop_ctrl_type_name(int type) {
  switch (type) {
    case CTRL_BUTTON:   return "CommandButton";
    case CTRL_CHECKBOX: return "CheckBox";
    case CTRL_LABEL:    return "Label";
    case CTRL_TEXTEDIT: return "TextBox";
    case CTRL_LIST:     return "ListBox";
    case CTRL_COMBOBOX: return "ComboBox";
    default:            return "Control";
  }
}

static form_element_t *prop_selected_element(form_doc_t *doc) {
  if (!doc || !doc->canvas_win) return NULL;
  canvas_state_t *cs = (canvas_state_t *)doc->canvas_win->userdata;
  if (!cs || cs->selected_idx < 0 || cs->selected_idx >= doc->element_count)
    return NULL;
  return &doc->elements[cs->selected_idx];
}

static void prop_add_row(window_t *list, const char *name, const char *value,
                         uint32_t prop_id) {
  const char *subs[1] = { value ? value : "" };
  reportview_item_t item = {
    .text = name ? name : "",
    .color = get_sys_color(brTextNormal),
    .userdata = prop_id,
    .subitems = { subs[0] },
    .subitem_count = 1,
  };
  send_message(list, RVM_ADDITEM, 0, &item);
}

static bool prop_row_editable(uint32_t prop_id) {
  switch (prop_id) {
    case PROP_ROW_NAME:
    case PROP_ROW_CAPTION:
    case PROP_ROW_LEFT:
    case PROP_ROW_TOP:
    case PROP_ROW_WIDTH:
    case PROP_ROW_HEIGHT:
      return true;
    default:
      return false;
  }
}

static int prop_parse_int(const char *s, int fallback) {
  if (!s || !*s) return fallback;
  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s) return fallback;
  if (v < INT16_MIN) v = INT16_MIN;
  if (v > INT16_MAX) v = INT16_MAX;
  return (int)v;
}

static void prop_end_edit(prop_browser_state_t *pbs, bool commit) {
  if (!pbs || !pbs->edit_win)
    return;

  window_t *edit = pbs->edit_win;
  form_doc_t *doc = g_app ? g_app->doc : NULL;
  form_element_t *el = prop_selected_element(doc);
  uint32_t prop_id = pbs->edit_prop_id;
  char value[sizeof(edit->title)];
  snprintf(value, sizeof(value), "%s", edit->title);

  pbs->edit_win = NULL;
  pbs->edit_prop_id = PROP_ROW_NONE;
  pbs->edit_row = -1;
  destroy_window(edit);

  if (!commit || !doc || !el)
    return;

  switch (prop_id) {
    case PROP_ROW_NAME:
      snprintf(el->name, sizeof(el->name), "%s", value);
      break;
    case PROP_ROW_CAPTION:
      snprintf(el->text, sizeof(el->text), "%s", value);
      break;
    case PROP_ROW_LEFT:
      el->frame.x = prop_parse_int(value, el->frame.x);
      if (el->frame.x < 0) el->frame.x = 0;
      if (el->frame.x + el->frame.w > doc->form_size.w)
        el->frame.x = doc->form_size.w - el->frame.w;
      if (el->frame.x < 0) el->frame.x = 0;
      break;
    case PROP_ROW_TOP:
      el->frame.y = prop_parse_int(value, el->frame.y);
      if (el->frame.y < 0) el->frame.y = 0;
      if (el->frame.y + el->frame.h > doc->form_size.h)
        el->frame.y = doc->form_size.h - el->frame.h;
      if (el->frame.y < 0) el->frame.y = 0;
      break;
    case PROP_ROW_WIDTH:
      el->frame.w = prop_parse_int(value, el->frame.w);
      if (el->frame.w < 10) el->frame.w = 10;
      if (el->frame.x + el->frame.w > doc->form_size.w)
        el->frame.w = doc->form_size.w - el->frame.x;
      if (el->frame.w < 1) el->frame.w = 1;
      break;
    case PROP_ROW_HEIGHT:
      el->frame.h = prop_parse_int(value, el->frame.h);
      if (el->frame.h < 8) el->frame.h = 8;
      if (el->frame.y + el->frame.h > doc->form_size.h)
        el->frame.h = doc->form_size.h - el->frame.y;
      if (el->frame.h < 1) el->frame.h = 1;
      break;
    default:
      return;
  }

  doc->modified = true;
  form_doc_update_title(doc);
  canvas_sync_live_controls(doc);
  property_browser_refresh(doc);
}

static void prop_begin_edit(prop_browser_state_t *pbs, int row) {
  if (!pbs || !pbs->list_win || row < 0)
    return;

  reportview_item_t item = {0};
  if (!send_message(pbs->list_win, RVM_GETITEMDATA, (uint32_t)row, &item))
    return;
  if (!prop_row_editable(item.userdata))
    return;

  prop_end_edit(pbs, false);

  int value_x = PROP_VALUE_X;
  int y = PROP_HEADER_H
        + row * COLUMNVIEW_ENTRY_HEIGHT
        - (int)pbs->list_win->scroll[1];
  int value_w = pbs->list_win->frame.w - value_x
              - (pbs->list_win->vscroll.visible ? SCROLLBAR_WIDTH : 0);
  if (value_w < 20) value_w = 20;

  pbs->edit_win = create_window(
      item.subitem_count > 0 && item.subitems[0] ? item.subitems[0] : "",
      WINDOW_NOTITLE,
      MAKERECT(value_x, y, value_w, COLUMNVIEW_ENTRY_HEIGHT),
      pbs->list_win, prop_edit_proc, 0, NULL);
  if (!pbs->edit_win)
    return;

  pbs->edit_win->id = 1;
  pbs->edit_win->userdata = pbs;
  pbs->edit_prop_id = item.userdata;
  pbs->edit_row = row;
  resize_window(pbs->edit_win, value_w, COLUMNVIEW_ENTRY_HEIGHT);
  pbs->edit_win->editing = true;
  pbs->edit_win->cursor_pos = (int)strlen(pbs->edit_win->title);
  set_focus(pbs->edit_win);
}

static void prop_fill_for_element(window_t *list, form_element_t *el) {
  char buf[32];

  prop_add_row(list, "(Name)", el->name, PROP_ROW_NAME);
  prop_add_row(list, "Caption", el->text, PROP_ROW_CAPTION);
  prop_add_row(list, "Type", prop_ctrl_type_name(el->type), PROP_ROW_TYPE);

  snprintf(buf, sizeof(buf), "%d", el->id);
  prop_add_row(list, "ID", buf, PROP_ROW_ID);
  snprintf(buf, sizeof(buf), "%d", el->frame.x);
  prop_add_row(list, "Left", buf, PROP_ROW_LEFT);
  snprintf(buf, sizeof(buf), "%d", el->frame.y);
  prop_add_row(list, "Top", buf, PROP_ROW_TOP);
  snprintf(buf, sizeof(buf), "%d", el->frame.w);
  prop_add_row(list, "Width", buf, PROP_ROW_WIDTH);
  snprintf(buf, sizeof(buf), "%d", el->frame.h);
  prop_add_row(list, "Height", buf, PROP_ROW_HEIGHT);
}

void property_browser_refresh(form_doc_t *doc) {
  if (!g_app || !g_app->prop_win || !is_window(g_app->prop_win))
    return;
  prop_browser_state_t *pbs = (prop_browser_state_t *)g_app->prop_win->userdata;
  if (!pbs || !pbs->list_win)
    return;

  prop_end_edit(pbs, false);

  window_t *list = pbs->list_win;
  send_message(list, RVM_SETREDRAW, 0, NULL);
  send_message(list, RVM_CLEAR, 0, NULL);

  form_element_t *el = prop_selected_element(doc);
  if (el) {
    prop_fill_for_element(list, el);
  } else {
    prop_add_row(list, "Selection", "(None)", PROP_ROW_NONE);
  }

  send_message(list, RVM_SETREDRAW, 1, NULL);
}

window_t *property_browser_create(hinstance_t hinstance) {
  window_t *win = create_window(
      "Properties",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PROPBROWSER_WIN_X, PROPBROWSER_WIN_Y,
               PROPBROWSER_WIN_W, PROPBROWSER_WIN_H),
      NULL, win_property_browser_proc, hinstance, NULL);
  if (win)
    show_window(win, true);
  return win;
}

result_t win_property_browser_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
  prop_browser_state_t *pbs = (prop_browser_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      pbs = allocate_window_data(win, sizeof(prop_browser_state_t));
      irect16_t cr = get_client_rect(win);
      pbs->list_win = create_window(
          "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(0, 0, cr.w, cr.h),
          win, win_reportview, 0, NULL);
      if (!pbs->list_win)
        return false;

      send_message(pbs->list_win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
      send_message(pbs->list_win, RVM_SETCOLUMNTITLESVISIBLE, 0, NULL);
      reportview_column_t c0 = { "Property", 72 };
      reportview_column_t c1 = { "Value", 0 };
      send_message(pbs->list_win, RVM_ADDCOLUMN, 0, &c0);
      send_message(pbs->list_win, RVM_ADDCOLUMN, 0, &c1);
      property_browser_refresh(g_app ? g_app->doc : NULL);
      return true;
    }

    case evDestroy:
      if (g_app && g_app->prop_win == win)
        g_app->prop_win = NULL;
      return false;

    case evResize:
      if (pbs && pbs->list_win) {
        irect16_t cr = get_client_rect(win);
        resize_window(pbs->list_win, cr.w, cr.h);
      }
      return false;

    case evCommand: {
      uint16_t notif = HIWORD(wparam);
      if (!pbs)
        return false;

      if (lparam == pbs->edit_win && notif == edUpdate) {
        prop_end_edit(pbs, true);
        return true;
      }

      if (lparam != pbs->list_win || notif != RVN_DBLCLK)
        return false;

      reportview_item_t item = {0};
      if (!send_message(pbs->list_win, RVM_GETITEMDATA, LOWORD(wparam), &item))
        return false;
      if (prop_row_editable(item.userdata))
        prop_begin_edit(pbs, (int)LOWORD(wparam));
      return true;
    }

    case evParentNotify: {
      if (!pbs || !lparam) return false;
      parent_notify_t *pn = (parent_notify_t *)lparam;
      if (pn->child != pbs->list_win)
        return false;

      if (pn->child_msg == evLeftButtonDown ||
          pn->child_msg == evLeftButtonDoubleClick) {
        int lx = (int16_t)LOWORD(pn->child_wparam);
        int ly = (int16_t)HIWORD(pn->child_wparam);
        if (lx < PROP_VALUE_X || ly < PROP_HEADER_H)
          return false;
        int row = (ly - PROP_HEADER_H) / COLUMNVIEW_ENTRY_HEIGHT;
        prop_begin_edit(pbs, row);
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

static result_t prop_edit_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  prop_browser_state_t *pbs = (prop_browser_state_t *)win->userdata;

  switch (msg) {
    case evDestroy:
      if (pbs && pbs->edit_win == win) {
        pbs->edit_win = NULL;
        pbs->edit_prop_id = PROP_ROW_NONE;
        pbs->edit_row = -1;
      }
      return win_textedit(win, msg, wparam, lparam);

    case evKillFocus:
      prop_end_edit(pbs, true);
      return true;

    case evKeyDown:
      if (wparam == AX_KEY_ESCAPE) {
        prop_end_edit(pbs, false);
        return true;
      }
      return win_textedit(win, msg, wparam, lparam);

    default:
      return win_textedit(win, msg, wparam, lparam);
  }
}
