// Project forms browser for the Orion Form Editor.
//
// Shows the forms loaded from a .orion project.

#include "formeditor.h"
#include <orion/commctl/commctl.h>
#include <orion/user/icons.h>

#define FORMS_ID_NEW     1
#define FORMS_ID_DELETE  2

typedef enum {
  OBJECT_ROW_FORM = 1,
  OBJECT_ROW_DATABASE = 2,
} object_row_kind_t;

#define OBJECT_ROWS_MAX (MAX_ELEMENTS + FE_MAX_PROJECT_DATABASES)

typedef struct {
  window_t *list_win;
  int       subscription_id;
  int       row_count;
  uint8_t   row_kind[OBJECT_ROWS_MAX];
  int16_t   row_index[OBJECT_ROWS_MAX];
} forms_browser_state_t;

static const toolbar_item_t kFormsToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, FORMS_ID_NEW,    "plus",  0, 0, "New form" },
  { TOOLBAR_ITEM_BUTTON, FORMS_ID_DELETE, "trash", 0, 0, "Delete form" },
};

static int forms_doc_count(void) {
  return g_app ? g_app->form_count : 0;
}

static window_t *forms_doc_at(int idx) {
  if (!g_app || idx < 0 || idx >= g_app->form_count)
    return NULL;
  return g_app->forms[idx];
}

static const char *forms_doc_label(window_t *doc) {
  if (!doc || !doc->title[0])
    return "Untitled";
  return doc->title;
}

static const char *project_database_label(int idx) {
  if (!g_app || idx < 0 || idx >= g_app->project.database_count)
    return "Unnamed";
  db_t *db = g_app->project.databases[idx];
  const char *name = db ? db->name : NULL;
  return (name && name[0]) ? name : "Unnamed";
}

static void objects_row_add(forms_browser_state_t *st,
                            object_row_kind_t kind,
                            int idx,
                            const char *label,
                            uint32_t color,
                            bool selected) {
  if (!st || !st->list_win || st->row_count >= OBJECT_ROWS_MAX)
    return;

  reportview_item_t item = {0};
  item.text = label;
  item.color = color;
  item.userdata = (uint32_t)st->row_count;
  send_message(st->list_win, RVM_ADDITEM, 0, &item);

  st->row_kind[st->row_count] = (uint8_t)kind;
  st->row_index[st->row_count] = (int16_t)idx;
  if (selected)
    send_message(st->list_win, RVM_SETSELECTION, (uint32_t)st->row_count, NULL);
  st->row_count++;
}

static void forms_browser_rebuild(forms_browser_state_t *st) {
  if (!st || !st->list_win) return;

  send_message(st->list_win, RVM_SETREDRAW, 0, NULL);
  send_message(st->list_win, RVM_CLEAR, 0, NULL);
  st->row_count = 0;

  for (int idx = 0; g_app && idx < g_app->form_count; idx++) {
    window_t *doc = g_app->forms[idx];
    if (!doc || !fe_doc_state(doc))
      continue;
    char label[576];
    snprintf(label, sizeof(label), "Form: %s", forms_doc_label(doc));
    objects_row_add(st,
                    OBJECT_ROW_FORM,
                    idx,
                    label,
                    get_sys_color(brTextNormal),
                    g_app && doc == g_app->active_form);
  }

  for (int idx = 0; g_app && idx < g_app->project.database_count; idx++) {
    char label[576];
    snprintf(label, sizeof(label), "Database: %s", project_database_label(idx));
    objects_row_add(st,
                    OBJECT_ROW_DATABASE,
                    idx,
                    label,
                    get_sys_color(brTextNormal),
                    false);
  }

  send_message(st->list_win, RVM_SETREDRAW, 1, NULL);
}

void forms_browser_refresh(void) {
  if (!g_app || !g_app->windows[FE_WIN_FORMS]) return;
  forms_browser_state_t *st = (forms_browser_state_t *)g_app->windows[FE_WIN_FORMS]->userdata;
  forms_browser_rebuild(st);
}

static void forms_browser_observer(fe_event_type_t event, window_t *doc, void *ctx) {
  (void)doc;
  (void)ctx;
  switch (event) {
    case FE_EVENT_DOCUMENT_CREATED:
    case FE_EVENT_DOCUMENT_CLOSED:
    case FE_EVENT_DOCUMENT_ACTIVATED:
    case FE_EVENT_DOCUMENT_MODIFIED:
      forms_browser_refresh();
      break;
    default:
      break;
  }
}

window_t *forms_browser_create(hinstance_t hinstance) {
  window_t *win = create_window("Objects",
      WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(FORMS_WIN_X, FORMS_WIN_Y, FORMS_WIN_W, FORMS_WIN_H),
      NULL, win_forms_browser_proc, hinstance, NULL);
  if (win) show_window(win, true);
  return win;
}

static void forms_add_new(void) {
  if (!g_app) return;
  window_t *doc = create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
  int n = forms_doc_count();
  snprintf(doc->title, sizeof(doc->title), "Form %d", n);
  fe_doc_mark_modified(doc);
  g_app->project.modified = true;
  forms_browser_refresh();
}

static void forms_delete_active(void) {
  if (!g_app || !g_app->active_form) return;
  window_t *doc = g_app->active_form;
  close_form_doc(doc);
  g_app->project.modified = true;
  forms_browser_refresh();
}

lresult_t win_forms_browser_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  forms_browser_state_t *st = (forms_browser_state_t *)win->userdata;
  (void)lparam;
  switch (msg) {
    case evCreate: {
      st = allocate_window_data(win, sizeof(forms_browser_state_t));
      if (!st)
        return false;

      irect16_t cr = get_client_rect(win);
      st->list_win = create_window(
          "", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(0, 0, cr.w, cr.h),
          win, win_reportview, 0, NULL);
      if (!st->list_win)
        return false;

      send_message(st->list_win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
      send_message(st->list_win, RVM_SETCOLUMNTITLESVISIBLE, 0, NULL);
      {
        reportview_column_t c0 = { "Object", 0 };
        send_message(st->list_win, RVM_ADDCOLUMN, 0, &c0);
      }

      send_message(win, tbSetItems, ARRAY_LEN(kFormsToolbar),
                   (void *)kFormsToolbar);
      st->subscription_id = fe_subscribe(forms_browser_observer, win);
      forms_browser_rebuild(st);
      return true;
    }

    case evResize:
      if (st && st->list_win) {
        irect16_t cr = get_client_rect(win);
        resize_window(st->list_win, cr.w, cr.h);
      }
      return false;

    case evCommand: {
      uint16_t notif = HIWORD(wparam);
      if (notif == btnClicked) {
        switch (LOWORD(wparam)) {
          case FORMS_ID_NEW:    forms_add_new(); return true;
          case FORMS_ID_DELETE: forms_delete_active(); return true;
          default: return false;
        }
      }
      if (!st || lparam != st->list_win)
        return false;
      if (notif == RVN_SELCHANGE)
        return true;
      if (notif != RVN_DBLCLK)
        return false;

      int row = (int)LOWORD(wparam);
      if (row < 0 || !st || row >= st->row_count)
        return false;

      if (st->row_kind[row] == OBJECT_ROW_FORM) {
        window_t *doc = forms_doc_at(st->row_index[row]);
        if (!doc)
          return false;
        form_doc_activate(doc);
        show_window(doc, true);
        forms_browser_refresh();
        return true;
      }

      if (st->row_kind[row] == OBJECT_ROW_DATABASE) {
        formeditor_show_database_object_window(st->row_index[row]);
        return true;
      }

      return false;
    }

    case evDestroy:
      formeditor_close_database_object_window();
      if (st)
        fe_unsubscribe(st->subscription_id);
      if (g_app && g_app->windows[FE_WIN_FORMS] == win)
        g_app->windows[FE_WIN_FORMS] = NULL;
      return false;

    default:
      return false;
  }
}
