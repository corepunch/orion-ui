#include <editor/editor.h>

enum {
  ID_LINE_TYPE = 1000,
  ID_LINE_ARG1,
  ID_LINE_ARG2,
  ID_LINE_ARG3,
  ID_LINE_ARG4,
  ID_LINE_ARG5,
  ID_LINE_START,
  ID_LINE_END,
  ID_LINE_FRONT_X,
  ID_LINE_FRONT_Y,
  ID_LINE_FRONT_BTM,
  ID_LINE_FRONT_MID,
  ID_LINE_FRONT_TOP,
  ID_LINE_BACK_X,
  ID_LINE_BACK_Y,
  ID_LINE_BACK_BTM,
  ID_LINE_BACK_MID,
  ID_LINE_BACK_TOP,
  ID_LINE_IDENT
};

#define ICON 44

windef_t line_layout[] = {
  { "win_label", "LineDef#: ", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_LINE_IDENT, 50 },
  { "win_label", "Type:", -1 },
  { "win_textedit", "", ID_LINE_TYPE, 32 },
  { "win_space" },
  { "win_label", "Arguments:", -1, WIDTH_FILL },
  { "win_textedit", "", ID_LINE_ARG1, 24 },
  { "win_textedit", "", ID_LINE_ARG2, 24 },
  { "win_textedit", "", ID_LINE_ARG3, 24 },
  { "win_textedit", "", ID_LINE_ARG4, 24 },
  { "win_textedit", "", ID_LINE_ARG5, 24 },
  { "win_space" },
  { "win_label", "Vertices:", -1 },
  { "win_textedit", "", ID_LINE_START, 40 },
  { "win_textedit", "", ID_LINE_END, 40 },
  { "win_space" },
  { "win_label", "Front:", -1, WIDTH_FILL },
  { "win_label", "x:", -1 },
  { "win_textedit", "", ID_LINE_FRONT_X, 32 },
  { "win_label", "y:", -1 },
  { "win_textedit", "", ID_LINE_FRONT_Y, 32 },
  { "win_space" },
  { "win_sprite", "", ID_LINE_FRONT_BTM, ICON, ICON },
  { "win_sprite", "", ID_LINE_FRONT_MID, ICON, ICON },
  { "win_sprite", "", ID_LINE_FRONT_TOP, ICON, ICON },
  { "win_label", "Back:", -1, WIDTH_FILL },
  { "win_label", "x:", -1 },
  { "win_textedit", "", ID_LINE_BACK_X, 32 },
  { "win_label", "y:", -1 },
  { "win_textedit", "", ID_LINE_BACK_Y, 32 }, { "win_space" },
  { "win_sprite", "", ID_LINE_BACK_BTM, ICON, ICON },
  { "win_sprite", "", ID_LINE_BACK_MID, ICON, ICON },
  { "win_sprite", "", ID_LINE_BACK_TOP, ICON, ICON },
  { NULL }
};

maplinedef_t *selected_line(game_t *game) {
  if (has_selection(game->state.hover, ObjTypeLine)) {
    return &game->map.linedefs[game->state.hover.index];
  } else if (has_selection(game->state.selected, ObjTypeLine)) {
    return &game->map.linedefs[game->state.selected.index];
  } else {
    return NULL;
  }
}

result_t win_dummy(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

result_t win_line(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *editor = get_editor();
  maplinedef_t *line = selected_line(g_game);
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      g_inspector = win;
      load_window_children(win, line_layout);
      return true;
    case evPaint:
      if (line) {
        set_window_item_text(win, ID_LINE_IDENT, "%d", line - g_game->map.linedefs);
        set_window_item_text(win, ID_LINE_TYPE, "%d", line->special);
        set_window_item_text(win, ID_LINE_TYPE, "%d", line->special);
        set_window_item_text(win, ID_LINE_START, "%d", line->start);
        set_window_item_text(win, ID_LINE_END, "%d", line->end);
        for (int i = 0; i < 5; i++) {
          set_window_item_text(win, ID_LINE_ARG1+i, "%d", line->args[i]);
        }
        if (line->sidenum[0] != 0xFFFF && g_game) {
          mapsidedef_t *front = &g_game->map.sidedefs[line->sidenum[0]];
          set_window_item_text(win, ID_LINE_FRONT_X, "%d", front->textureoffset);
          set_window_item_text(win, ID_LINE_FRONT_Y, "%d", front->rowoffset);
          set_window_item_text(win, ID_LINE_FRONT_TOP, front->toptexture);
          set_window_item_text(win, ID_LINE_FRONT_MID, front->midtexture);
          set_window_item_text(win, ID_LINE_FRONT_BTM, front->bottomtexture);
        }
        if (line->sidenum[1] != 0xFFFF && g_game) {
          mapsidedef_t *back = &g_game->map.sidedefs[line->sidenum[1]];
          set_window_item_text(win, ID_LINE_BACK_X, "%d", back->textureoffset);
          set_window_item_text(win, ID_LINE_BACK_Y, "%d", back->rowoffset);
          set_window_item_text(win, ID_LINE_BACK_TOP, back->toptexture);
          set_window_item_text(win, ID_LINE_BACK_MID, back->midtexture);
          set_window_item_text(win, ID_LINE_BACK_BTM, back->bottomtexture);
        }
      }
      return false;
    case evCommand:
      if (line) {
#define SET_OFFSET(ID, SIDE, OFFSET) \
        if (wparam == MAKEDWORD(ID, edUpdate) && line->sidenum[SIDE] != 0xFFFF && g_game) { \
          mapsidedef_t *side = &g_game->map.sidedefs[line->sidenum[SIDE]]; \
          side->OFFSET = atoi(((window_t *)lparam)->title); \
          build_wall_vertex_buffer(&g_game->map); \
          build_floor_vertex_buffer(&g_game->map); \
          invalidate_window(editor->window); \
        }
        SET_OFFSET(ID_LINE_FRONT_X, 0, textureoffset);
        SET_OFFSET(ID_LINE_FRONT_Y, 0, rowoffset);
        SET_OFFSET(ID_LINE_BACK_X, 1, textureoffset);
        SET_OFFSET(ID_LINE_BACK_Y, 1, rowoffset);
      }
      return true;
  }
  return win_dummy(win, msg, wparam, lparam);
}
