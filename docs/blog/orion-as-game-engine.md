---
layout: default
title: Orion as a Multiwindow Retro Gaming Engine
parent: Blog
nav_order: 1
date: 2026-04-27
---

# Orion as a Multiwindow Retro Gaming Engine

*April 27, 2026*

---

Most game engines give you a single fullscreen surface and ask you to build everything on top of it. Orion takes a different bet: what if every panel, viewport, and HUD element were a genuine window — a first-class citizen with its own message queue, paint cycle, and event handler?

That question turns out to be surprisingly powerful for a certain genre of game.

## The Pitch

Imagine *Dungeon Master* or *Ultima Underworld* — a dungeon crawler where the 3D viewport, the map, the character stats, and the inventory all live in resizable, moveable windows. Or a strategy game in the vein of *Civilization* or *Master of Magic* where the tech tree, the city editor, and the main map are genuinely separate windows you can drag around a desktop. Or a space trader where the cargo manifest is an actual spreadsheet you scroll through.

This is the **Multiwindow Retro Gaming Engine** pattern: use Orion's WinAPI-style windowing system as the backbone of your game, and let each game panel be an Orion window.

## Why Orion Fits

### 1. The Message Loop Is the Game Loop

A classic game loop looks like this:

```c
while (running) {
    process_input();
    update();
    render();
}
```

Orion's main loop is:

```c
msg_t msg;
while (get_message(&msg)) {
    dispatch_message(&msg);
}
```

These are the same thing. `get_message` pumps SDL events, translates them into Orion messages (`evLeftButtonDown`, `evKeyDown`, `evTimer`, …), and dispatches them to the correct window procedure. Your game state update and render happen inside window procedures, triggered by messages. There is no impedance mismatch — the framework's loop and the game's loop are identical.

### 2. Windows Are Game Entities

In Orion every window has:

- A **proc** function (`winproc_t`) — essentially `OnMessage()`
- A **userdata** pointer — your game-object state
- A **frame** rect — position and size on screen
- An **invalidate/paint** cycle — dirty-flag rendering, exactly what games want

Sound familiar? This is the same pattern as a game-object component with `update()` and `draw()`. The window procedure *is* the component update loop, and `evPaint` *is* the draw callback.

```c
// A game viewport as an Orion window
static result_t dungeon_view_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
    dungeon_state_t *st = (dungeon_state_t *)win->userdata;
    switch (msg) {
        case evCreate:
            st = calloc(1, sizeof(dungeon_state_t));
            win->userdata = st;
            load_level(st, "level01.dat");
            return true;
        case evPaint:
            render_3d_view(st, &win->frame);
            return true;
        case evKeyDown:
            handle_movement(st, (SDL_Keycode)wparam);
            invalidate_window(win);
            return true;
        case evDestroy:
            free(st);
            return true;
    }
    return false;
}
```

### 3. OpenGL Rendering Is Already There

Orion uses OpenGL 3.2+ through a clean renderer abstraction (`kernel/renderer.h`). Textures, meshes, sprite batches — they're all there. Your 3D dungeon view, your sprite-based overworld, or your tile map render directly to the window's screen region using the same pipeline the UI chrome uses. No context-switching, no separate render pass.

### 4. Joystick and Gamepad Support

`kernel/joystick.c` is already in the tree. SDL2 gamepad events are translated into Orion messages before they reach your window procedure. A platformer or a twin-stick shooter just handles `evJoystickAxis` and `evJoystickButton` the same way it handles keyboard input.

### 5. The Retro Aesthetic Is Built In

Orion ships with a bitmap font (`Geneva-9`/`ChiKareGo2`), a pixel-art icon sheet, and a dark color scheme that looks like a 1992 workstation. If your game *wants* to look like it's running on AmigaOS or NeXTSTEP — complete with draggable windows and a title bar — you get that for free.

## A Concrete Blueprint: Four-Window Dungeon Crawler

Here is how you'd structure a classic dungeon crawler using Orion:

```
┌──────────────────────────────────────┐
│  Dungeon Master Clone                │
├──────────────────┬───────────────────┤
│                  │   Mini-map        │
│  3D Viewport     ├───────────────────┤
│  (dungeon_view)  │   Character Stats │
│                  │   (stats_panel)   │
├──────────────────┴───────────────────┤
│  Inventory Grid  (inventory_panel)   │
└──────────────────────────────────────┘
```

Each panel is an Orion window:

| Window | proc | State |
|---|---|---|
| `dungeon_view` | `dungeon_view_proc` | 3D renderer state, current room |
| `minimap` | `minimap_proc` | Fog-of-war bitmap |
| `stats_panel` | `stats_proc` | HP, MP, status effects |
| `inventory_panel` | `inventory_proc` | Item grid, drag-and-drop |

They communicate via `send_message` / `post_message` — the exact same channel used by buttons and checkboxes. The inventory window posts `evCommand` to the stats window when a potion is used. The dungeon view sends a custom `evRoomChanged` message to the minimap when the player walks through a door.

Because they're real Orion windows, the player can also tear them off, resize them, and rearrange them — giving the game the same feel as *Ultima VII*'s famously flexible GUI.

## Beyond the Dungeon

The multiwindow pattern applies far beyond RPGs:

- **Space trader** — main star map window + trade window (a real `win_reportview` spreadsheet) + ship status panel
- **Real-time strategy** — main map + construction queue (a `win_list`) + minimap + tech tree dialog
- **Programming game** — code editor window (`win_edit`) + execution log (`win_console`) + visualisation canvas
- **Puzzle game** — game board window + hint panel + move history list

In every case you get scrollbars, resize handles, keyboard focus, menus, and all the other plumbing for free.

## Getting Started

The fastest path is to clone Orion and look at the `examples/` directory. `helloworld.c` shows the window creation and message loop basics. `filemanager.c` demonstrates a multi-panel layout with a column-view control — structurally identical to the inventory-grid pattern above.

```bash
git clone https://github.com/corepunch/orion-ui
cd orion-ui
make examples
./build/bin/filemanager
```

Then replace the file-list data with your game data, replace the file-icon sprites with your tile set, and you already have a scrollable, selectable game-item grid.

## Closing Thought

Orion was extracted from DOOM-ED, the level editor for DOOM. DOOM-ED itself was a multi-window application: the map editor, the thing editor, the texture browser, the console — all windows. It turns out the same architecture that makes a great *tool* also makes a great *game*. The windows are just your viewports, and the message loop is your game loop.

If that idea resonates with you, [grab the source](https://github.com/corepunch/orion-ui) and start building.

---

*Tagged: game engine, retro, architecture, C, SDL2, OpenGL*
