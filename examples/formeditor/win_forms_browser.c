// Project forms browser for the Orion Form Editor.
//
// Shows the forms loaded from a .orion project.

#include "formeditor.h"
#include "../../commctl/commctl.h"
#include "../../user/icons.h"

#define FORMS_ID_NEW     1
#define FORMS_ID_DELETE  2
#define FORMS_ROW_Y      4
#define FORMS_ROW_H      18

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

void forms_browser_refresh(void) {
  if (g_app && g_app->forms_win)
    invalidate_window(g_app->forms_win);
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
  (void)lparam;
  switch (msg) {
    case evCreate:
      send_message(win, tbSetItems, ARRAY_LEN(kFormsToolbar),
                   (void *)kFormsToolbar);
      return true;

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

    case evPaint: {
      fill_rect(get_sys_color(brWindowBg), R(0, 0, win->frame.w, win->frame.h));
      int idx = 0;
      for (form_doc_t *doc = g_app ? g_app->docs : NULL; doc; doc = doc->next, idx++) {
        irect16_t row = R(4, FORMS_ROW_Y + idx * FORMS_ROW_H,
                          win->frame.w - 8, FORMS_ROW_H);
        if (doc == g_app->doc) {
          fill_rect(get_sys_color(brTextNormal), row);
          draw_text_clipped(FONT_SMALL, forms_doc_label(doc), &row,
                            get_sys_color(brWindowBg), TEXT_PADDING_LEFT);
        } else {
          draw_text_clipped(FONT_SMALL, forms_doc_label(doc), &row,
                            get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
        }
      }
      return true;
    }

    case evLeftButtonDown: {
      int y = (int16_t)HIWORD(wparam);
      if (y < FORMS_ROW_Y) return false;
      int idx = (y - FORMS_ROW_Y) / FORMS_ROW_H;
      form_doc_t *doc = forms_doc_at(idx);
      if (doc) {
        form_doc_activate(doc);
        if (doc->doc_win) show_window(doc->doc_win, true);
        forms_browser_refresh();
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}
