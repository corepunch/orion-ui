#include <editor/editor.h>

enum {
  ID_VERTEX_ID = 1000,
  ID_VERTEX_POS_X,
  ID_VERTEX_POS_Y,
};

windef_t vertex_layout[] = {
  { "win_label", "Vertex#:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_VERTEX_ID, 50 },
  { "win_label", "Position X:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_VERTEX_POS_X, 50 },
  { "win_label", "Position Y:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_VERTEX_POS_Y, 50 },
  { NULL }
};

mapvertex_t *selected_vertex(game_t *game) {
  if (has_selection(game->state.hover, ObjTypePoint)) {
    return &game->map.vertices[game->state.hover.index];
  } else if (has_selection(game->state.selected, ObjTypePoint)) {
    return &game->map.vertices[game->state.selected.index];
  } else {
    return NULL;
  }
}

result_t win_vertex(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *editor = get_editor();
  mapvertex_t *point = selected_vertex(g_game);
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      g_inspector = win;
      load_window_children(win, vertex_layout);
      return true;
    case evPaint:
      if (point) {
        set_window_item_text(win, ID_VERTEX_ID, "%d", point - g_game->map.vertices);
        set_window_item_text(win, ID_VERTEX_POS_X, "%d", point->x);
        set_window_item_text(win, ID_VERTEX_POS_Y, "%d", point->y);
      }
      return false;
    case evCommand:
      if (point) {
        if (wparam == MAKEDWORD(ID_VERTEX_POS_X, edUpdate)) {
          point->x = atoi(((window_t *)lparam)->title);
          invalidate_window(editor->window);
        }
        if (wparam == MAKEDWORD(ID_VERTEX_POS_Y, edUpdate)) {
          point->y = atoi(((window_t *)lparam)->title);
          invalidate_window(editor->window);
        }
      }
      return true;
  }
  return false;
}
