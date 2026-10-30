#include <editor/editor.h>
#include <mapview/sprites.h>

#define MTF_EASY    1
#define MTF_NORMAL    2
#define MTF_HARD    4
#define MTF_AMBUSH    8
#define MTF_DORMANT    16
#define MTF_FIGHTER    32
#define MTF_CLERIC    64
#define MTF_MAGE    128
#define MTF_GSINGLE    256
#define MTF_GCOOP    512
#define MTF_GDEATHMATCH  1024

enum {
  ID_THING_TYPE = 1000,
  ID_THING_POS_X,
  ID_THING_POS_Y,
  ID_THING_ANGLE,
  ID_THING_SPRITE,
  ID_THING_EASY,
  ID_THING_NORMAL,
  ID_THING_HARD,
  ID_THING_FIGHTER,
  ID_THING_CLERIC,
  ID_THING_MAGE,
  ID_THING_GSINGLE,
  ID_THING_GCOOP,
  ID_THING_GDEATHMATCH,
  ID_THING_AMBUSH,
  ID_THING_DORMANT,
};

windef_t thing_layout[] = {
  { "win_label", "Type:", -1, LABEL_WIDTH },
  { "win_button", "Click me", ID_THING_TYPE, 50 },
  { "win_label", "Position X:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_THING_POS_X, 50 },
  { "win_label", "Position Y:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_THING_POS_Y, 50 },
  { "win_label", "Angle:", -1, LABEL_WIDTH },
  { "win_textedit", "", ID_THING_ANGLE, 50 },
  { "win_sprite", "", ID_THING_SPRITE, 64, 64 },
  { "win_space" },
  { "win_checkbox", "Easy", ID_THING_EASY, 64 },
  { "win_checkbox", "Medium", ID_THING_NORMAL, 64 },
  { "win_checkbox", "Hard", ID_THING_HARD, 64 },
  { "win_checkbox", "Fighter", ID_THING_FIGHTER, 64 },
  { "win_checkbox", "Cleric", ID_THING_CLERIC, 64 },
  { "win_checkbox", "Mage", ID_THING_MAGE, 64 },
  { "win_checkbox", "Single", ID_THING_GSINGLE, 64 },
  { "win_checkbox", "Coop", ID_THING_GCOOP, 64 },
  { "win_checkbox", "Deathmatch", ID_THING_GDEATHMATCH, 64 },
  { "win_checkbox", "Ambush", ID_THING_AMBUSH, 64 },
  { "win_checkbox", "Dormant", ID_THING_DORMANT, 64 },
  { NULL }
};

uint32_t thing_checkboxes[] = {
  ID_THING_EASY,
  ID_THING_NORMAL,
  ID_THING_HARD,
  ID_THING_AMBUSH,
  ID_THING_DORMANT,
  ID_THING_FIGHTER,
  ID_THING_CLERIC,
  ID_THING_MAGE,
  ID_THING_GSINGLE,
  ID_THING_GCOOP,
  ID_THING_GDEATHMATCH,
};

mapthing_t *selected_thing(game_t *game) {
  if (has_selection(game->state.hover, ObjTypeThing)) {
    return &game->map.things[game->state.hover.index];
  } else if (has_selection(game->state.selected, ObjTypeThing)) {
    return &game->map.things[game->state.selected.index];
  } else {
    return NULL;
  }
}

result_t win_things(window_t *, uint32_t, uint32_t, void *);

uint16_t select_thing_type(window_t *owner) {
  return show_dialog("Things", THING_SIZE*3, owner->frame.h-16, owner, win_things, NULL);
}

result_t win_dummy(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

result_t win_thing(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *editor = get_editor();
  mapthing_t *thing = selected_thing(g_game);
  uint16_t tmp;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      g_inspector = win;
      load_window_children(win, thing_layout);
      return true;
    case evPaint:
      if (thing) {
        sprite_t *spr = get_thing_sprite_name(thing->type, 0);
        set_window_item_text(win, ID_THING_SPRITE, spr?spr->name:"");
        set_window_item_text(win, ID_THING_TYPE, "%d", thing->height);
        set_window_item_text(win, ID_THING_POS_X, "%d", thing->x);
        set_window_item_text(win, ID_THING_POS_Y, "%d", thing->y);
        set_window_item_text(win, ID_THING_ANGLE, "%d", thing->angle);
        for (int i = 0; i < sizeof(thing_checkboxes)/sizeof(*thing_checkboxes); i++) {
          window_t *checkbox = get_window_item(win, thing_checkboxes[i]);
          uint32_t value = thing->options&(1<<i);
          send_message(checkbox, btnSetCheck, value, NULL);
        }
      }
      return false;
    case evCommand:
      if (thing) {
        for (int i = 0; i < sizeof(thing_checkboxes)/sizeof(*thing_checkboxes); i++) {
          if (wparam == MAKEDWORD(thing_checkboxes[i], btnClicked)) {
            if (send_message(lparam, btnGetCheck, 0, NULL)) {
              thing->options |= 1 << i;
            } else {
              thing->options &= ~(1 << i);
            }
          }
        }
        switch (wparam) {
          case MAKEDWORD(ID_THING_POS_X, edUpdate):
            thing->x = atoi(((window_t *)lparam)->title);
            invalidate_window(editor->window);
            break;
          case MAKEDWORD(ID_THING_POS_Y, edUpdate):
            thing->y = atoi(((window_t *)lparam)->title);
            invalidate_window(editor->window);
            break;
          case MAKEDWORD(ID_THING_ANGLE, edUpdate):
            thing->angle = atoi(((window_t *)lparam)->title);
            invalidate_window(editor->window);
            break;
          case MAKEDWORD(ID_THING_SPRITE, btnClicked):
            if ((tmp = select_thing_type(win)) != 0xFFFF) {
              thing->type = tmp;
              invalidate_window(editor->window);
            }
            break;
        }
      }
      return true;
  }
  return win_dummy(win, msg, wparam, lparam);
}
