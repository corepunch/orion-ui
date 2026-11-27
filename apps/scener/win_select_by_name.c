#include "scener.h"
#include <orion/commctl/commctl.h>
#include <stdlib.h>

enum { SELECT_BY_NAME_LIST = 7100, SELECT_BY_NAME_OK, SELECT_BY_NAME_CANCEL };

typedef struct {
  scene_doc_t *doc;
  window_t *list;
  int *objects;
  int count;
  int selected;
} select_by_name_state_t;

static result_t select_by_name_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  select_by_name_state_t *st = (select_by_name_state_t *)win->userdata;
  if (msg == evCreate) {
    st = (select_by_name_state_t *)lparam;
    win->userdata = st;
    st->list = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
        MAKERECT(8, 8, 404, 272), win, win_reportview, 0, NULL);
    if (st->list) {
      send_message(st->list, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
      send_message(st->list, RVM_SETCOLUMNTITLESVISIBLE, 1, NULL);
      reportview_column_t name = { ui_tr("Name"), 280 };
      reportview_column_t type = { ui_tr("Type"), 0 };
      send_message(st->list, RVM_ADDCOLUMN, 0, &name);
      send_message(st->list, RVM_ADDCOLUMN, 0, &type);
      for (int row = 0; row < st->count; row++) {
        SceneObj *obj = &st->doc->scene.objs[st->objects[row]];
        const char *subitems[] = { scene_node_tag(obj->editNode) };
        reportview_item_t item = {
          .text = scene_node_attr(obj->editNode, "name"),
          .color = get_sys_color(brTextNormal),
          .subitems = { subitems[0] },
          .subitem_count = 1
        };
        send_message(st->list, RVM_ADDITEM, 0, &item);
        if (st->objects[row] == st->doc->scene.selectedObj) {
          st->selected = row;
          send_message(st->list, RVM_SETSELECTION, row, NULL);
        }
      }
      if (st->selected >= 0 && st->selected < st->count)
        send_message(st->list, RVM_SETSELECTION, st->selected, NULL);
    }
    window_t *ok = create_window(ui_tr("Select"), WINDOW_NOTITLE,
        MAKERECT(250, 288, 78, 25), win, "Button", 0, NULL);
    window_t *cancel = create_window(ui_tr("Cancel"), WINDOW_NOTITLE,
        MAKERECT(334, 288, 78, 25), win, "Button", 0, NULL);
    if (ok) ok->id = SELECT_BY_NAME_OK;
    if (cancel) cancel->id = SELECT_BY_NAME_CANCEL;
    return true;
  }
  if (msg == evResize || msg == evArrange) {
    irect16_t client = get_client_rect(win);
    if (st && st->list) resize_window(st->list, client.w - 16, client.h - 48);
    window_t *ok = get_window_item(win, SELECT_BY_NAME_OK);
    window_t *cancel = get_window_item(win, SELECT_BY_NAME_CANCEL);
    if (ok) move_window(ok, client.w - 170, client.h - 34);
    if (cancel) move_window(cancel, client.w - 86, client.h - 34);
    return true;
  }
  if (msg == evCommand && lparam == (void *)st->list &&
      HIWORD(wparam) == RVN_SELCHANGE) {
    st->selected = (int)LOWORD(wparam);
    return true;
  }
  if (msg == evCommand && HIWORD(wparam) == RVN_DBLCLK) {
    if (lparam == (void *)st->list) {
      st->selected = (int)LOWORD(wparam);
      end_dialog(win, 1);
      return true;
    }
  }
  if (msg == evCommand && HIWORD(wparam) == btnClicked) {
    if (LOWORD(wparam) == SELECT_BY_NAME_CANCEL) { end_dialog(win, 0); return true; }
    if (LOWORD(wparam) == SELECT_BY_NAME_OK) {
      if (st->list) st->selected = (int)send_message(st->list, RVM_GETSELECTION, 0, NULL);
      end_dialog(win, 1);
      return true;
    }
  }
  return false;
}

void scener_select_by_name(scene_doc_t *doc) {
  if (!doc) return;
  Scene *scene = &doc->scene;
  int *objects = scene->nobjs ? malloc((size_t)scene->nobjs * sizeof(*objects)) : NULL;
  if (scene->nobjs && !objects) return;
  int count = 0;
  for (int i = 0; i < scene->nobjs; i++) {
    const char *name = scene_node_attr(scene->objs[i].editNode, "name");
    if (name && name[0]) objects[count++] = i;
  }
  if (!count) { free(objects); return; }
  select_by_name_state_t st = { .doc = doc, .objects = objects, .count = count, .selected = -1 };
  window_t *parent = doc->win ? doc->win : (g_app ? g_app->chrome_win : NULL);
  if (show_dialog(ui_tr("Select by Name"), 430, 330, parent,
      select_by_name_proc, &st) && st.selected >= 0 && st.selected < st.count) {
    scene->createMode = 0;
    scene->selectedObj = st.objects[st.selected];
    scene->selectedNode = scene->objs[scene->selectedObj].editNode;
    scene->selectedRigInstance = NULL;
    scene->selectedRigJoint = NULL;
    scener_inspect_selection();
    scener_sync_tool_ui();
    if (doc->viewport_win) invalidate_window(doc->viewport_win);
  }
  free(objects);
}
