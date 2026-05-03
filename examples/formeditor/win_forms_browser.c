// Project forms browser for the Orion Form Editor.
//
// Shows the forms loaded from a .orion project.

#include "formeditor.h"
#include "../../commctl/commctl.h"
#include "../../user/icons.h"

#define FORMS_ID_NEW     1
#define FORMS_ID_DELETE  2

typedef struct {
  window_t *list_win;
} forms_browser_state_t;

static const toolbar_item_t kFormsToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, FORMS_ID_NEW,    sysicon_add,    0, 0, "New form" },
  { TOOLBAR_ITEM_BUTTON, FORMS_ID_DELETE, sysicon_delete, 0, 0, "Delete form" },
};

static int forms_doc_count(void) {
  int n = 0;
  if (!g_app) return 0;
  for (form_doc_t *doc = g_app->docs; doc; doc = doc->next)
    n++;
  return n;
}

static form_doc_t *forms_doc_at(int idx) {
  int n = 0;
  if (!g_app) return NULL;
  for (form_doc_t *doc = g_app->docs; doc; doc = doc->next) {
    if (n == idx) return doc;
    n++;
  }
  return NULL;
}

static const char *forms_doc_label(form_doc_t *doc) {
  if (!doc) return "";
  if (doc->form_title[0]) return doc->form_title;
  if (doc->form_id[0]) return doc->form_id;
  return "Untitled";
}

static void forms_browser_rebuild(forms_browser_state_t *st) {
  if (!st || !st->list_win) return;

  send_message(st->list_win, RVM_SETREDRAW, 0, NULL);
  send_message(st->list_win, RVM_CLEAR, 0, NULL);

  int idx = 0;
  int selected = -1;
  for (form_doc_t *doc = g_app ? g_app->docs : NULL; doc; doc = doc->next, idx++) {
    reportview_item_t item = {0};
    item.text = forms_doc_label(doc);
    item.color = get_sys_color(brTextNormal);
    item.userdata = (uint32_t)idx;
    send_message(st->list_win, RVM_ADDITEM, 0, &item);
    if (g_app && doc == g_app->doc)
      selected = idx;
  }

  send_message(st->list_win, RVM_SETREDRAW, 1, NULL);
  if (selected >= 0)
    send_message(st->list_win, RVM_SETSELECTION, (uint32_t)selected, NULL);
}

void forms_browser_refresh(void) {
  if (!g_app || !g_app->forms_win) return;
  forms_browser_state_t *st = (forms_browser_state_t *)g_app->forms_win->userdata;
  forms_browser_rebuild(st);
}

window_t *forms_browser_create(hinstance_t hinstance) {
  window_t *win = create_window("Forms",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(FORMS_WIN_X, FORMS_WIN_Y, FORMS_WIN_W, FORMS_WIN_H),
      NULL, win_forms_browser_proc, hinstance, NULL);
  if (win) show_window(win, true);
  return win;
}

static void forms_add_new(void) {
  if (!g_app) return;
  form_doc_t *doc = create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
  if (!doc) return;
  int n = forms_doc_count();
  snprintf(doc->form_id, sizeof(doc->form_id), "form%d", n);
  snprintf(doc->form_title, sizeof(doc->form_title), "Form %d", n);
  doc->modified = true;
  g_app->project.modified = true;
  form_doc_update_title(doc);
  forms_browser_refresh();
}

static void forms_delete_active(void) {
  if (!g_app || !g_app->doc) return;
  form_doc_t *doc = g_app->doc;
  close_form_doc(doc);
  g_app->project.modified = true;
  forms_browser_refresh();
}

result_t win_forms_browser_proc(window_t *win, uint32_t msg,
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
        reportview_column_t c0 = { "Form", 0 };
        send_message(st->list_win, RVM_ADDCOLUMN, 0, &c0);
      }

      send_message(win, tbSetItems, ARRAY_LEN(kFormsToolbar),
                   (void *)kFormsToolbar);
      forms_browser_rebuild(st);
      return true;
    }

    case tbButtonClick:
      switch ((uint16_t)wparam) {
        case FORMS_ID_NEW:
          forms_add_new();
          return true;
        case FORMS_ID_DELETE:
          forms_delete_active();
          return true;
        default:
          return false;
      }

    case evResize:
      if (st && st->list_win) {
        irect16_t cr = get_client_rect(win);
        resize_window(st->list_win, cr.w, cr.h);
      }
      return false;

    case evCommand: {
      uint16_t notif = HIWORD(wparam);
      if (!st || lparam != st->list_win)
        return false;
      if (notif == RVN_SELCHANGE)
        return true;
      if (notif != RVN_DBLCLK)
        return false;

      form_doc_t *doc = forms_doc_at((int)LOWORD(wparam));
      if (!doc)
        return false;
      form_doc_activate(doc);
      if (doc->doc_win)
        show_window(doc->doc_win, true);
      forms_browser_refresh();
      return true;
    }

    case evDestroy:
      if (g_app && g_app->forms_win == win)
        g_app->forms_win = NULL;
      return false;

    default:
      return false;
  }
}
