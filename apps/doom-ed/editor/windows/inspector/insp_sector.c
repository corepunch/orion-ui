#include <editor/editor.h>

enum {
  ID_SECTOR_FLOOR_HEIGHT = 1000,
  ID_SECTOR_FLOOR_IMAGE,
  ID_SECTOR_CEILING_HEIGHT,
  ID_SECTOR_CEILING_IMAGE,
  ID_SECTOR_LIGHT_LEVEL,
  ID_TEST_COMBOBOX,
  ID_SECTOR_IDENT,
};

windef_t sector_layout[] = {
  { "win_label", "Sector#: ", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_SECTOR_IDENT, 50 },
  { "win_label", "Light lvl:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_SECTOR_LIGHT_LEVEL, 50 },
  { "win_label", "Floor Hgt:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_SECTOR_FLOOR_HEIGHT, 50 },
  { "win_label", "Ceiling Hgt:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_SECTOR_CEILING_HEIGHT, 50 },
//  { "win_combobox", "Stone", ID_TEST_COMBOBOX, 90 }, {"win_space"},
  { "win_sprite", "", ID_SECTOR_FLOOR_IMAGE, 64, 64 },
  { "win_sprite", "", ID_SECTOR_CEILING_IMAGE, 64, 64 },
  { NULL }
};

mapsector_t *selected_sector(game_t *game) {
  if (!g_game) {
    return NULL;
  } else if (has_selection(game->state.hover, ObjTypeSector)) {
    return &game->map.sectors[game->state.hover.index];
  } else if (has_selection(game->state.selected, ObjTypeSector)) {
    return &game->map.sectors[game->state.selected.index];
  } else {
    return NULL;
  }
}

//static void init_combobox(window_t *cb) {
//  send_message(cb, CB_ADDSTRING, 0, "worldspawn");
//  send_message(cb, CB_ADDSTRING, 0, "trigger_once");
//  send_message(cb, CB_ADDSTRING, 0, "func_door");
//  send_message(cb, CB_ADDSTRING, 0, "light");
//  send_message(cb, CB_ADDSTRING, 0, "misc_model");
//  send_message(cb, CB_ADDSTRING, 0, "path_corner");
//  send_message(cb, CB_ADDSTRING, 0, "env_spark");
//  send_message(cb, CB_ADDSTRING, 0, "func_train");
//  send_message(cb, CB_ADDSTRING, 0, "info_player_start");
//  send_message(cb, CB_ADDSTRING, 0, "target_teleport");
//  send_message(cb, CB_SETCURSEL, 2, NULL);
//}

static toolbar_item_t but[] = {
  { TOOLBAR_ITEM_BUTTON, EditModeSelect,   NULL, 0, TBSTYLE_CHECKGROUP, "Select" },
  { TOOLBAR_ITEM_BUTTON, EditModeVertices, NULL, 0, TBSTYLE_CHECKGROUP, "Vertices" },
  { TOOLBAR_ITEM_BUTTON, EditModeThings,   NULL, 0, TBSTYLE_CHECKGROUP, "Things" },
  { TOOLBAR_ITEM_BUTTON, EditModeSounds,   NULL, 0, TBSTYLE_CHECKGROUP, "Sounds" },
};

void set_selection_mode(editor_state_t *editor, int mode);

result_t win_dummy(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *editor = get_editor();
  switch (msg) {
    case evCreate:
      send_message(win, tbSetItems, sizeof(but)/sizeof(*but), but);      
      return true;
    case evPaint:
      draw_text_small("Nothing selected", 5, 5, get_sys_color(brDarkEdge));
      draw_text_small("Nothing selected", 4, 4, get_sys_color(brTextNormal));
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      wparam = LOWORD(wparam);
      send_message(win, tbCheckButton, wparam, (void *)1);
      set_selection_mode(editor, wparam);
      invalidate_window(win);
      return true;
    default:
      return false;
  }
}

result_t win_sector(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *editor = get_editor();
  mapsector_t *sector = selected_sector(g_game);
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      g_inspector = win;
      load_window_children(win, sector_layout);
//      init_combobox(get_window_item(win, ID_TEST_COMBOBOX));
      return true;
    case evPaint:
      if (sector) {
        set_window_item_text(win, ID_SECTOR_IDENT, "%d", sector - g_game->map.sectors);
        set_window_item_text(win, ID_SECTOR_LIGHT_LEVEL, "%d", sector->lightlevel);
        set_window_item_text(win, ID_SECTOR_FLOOR_HEIGHT, "%d", sector->floorheight);
        set_window_item_text(win, ID_SECTOR_FLOOR_IMAGE, sector->floorpic);
        set_window_item_text(win, ID_SECTOR_CEILING_HEIGHT, "%d", sector->ceilingheight);
        set_window_item_text(win, ID_SECTOR_CEILING_IMAGE, sector->ceilingpic);
      }
      return false;
    case evCommand:
      if (sector) {
        switch (wparam) {
          case MAKEDWORD(ID_SECTOR_LIGHT_LEVEL, edUpdate):
            sector->lightlevel = atoi(((window_t *)lparam)->title);
            break;
          case MAKEDWORD(ID_SECTOR_FLOOR_HEIGHT, edUpdate):
            sector->floorheight = atoi(((window_t *)lparam)->title);
            break;
          case MAKEDWORD(ID_SECTOR_CEILING_HEIGHT, edUpdate):
            sector->ceilingheight = atoi(((window_t *)lparam)->title);
            break;
        }
        if (g_game) {
          build_wall_vertex_buffer(&g_game->map);
          build_floor_vertex_buffer(&g_game->map);
          invalidate_window(editor->window);
        }
      }
      return true;
  }
  return win_dummy(win, msg, wparam, lparam);
}
