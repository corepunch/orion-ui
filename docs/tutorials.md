# Tutorials

A tutorial walks the user through a task one step at a time. Each step is a
speech bubble that points at one control or toolbar button, with a ring
(`Tutorial::spot`) around it. A scrim dims the window except around the ring.
The scrim only dims: the user can still click anything, including the control the step
explains. **Next** advances, **Skip** closes, and the
last step's **Done** finishes. The bubble follows its target when windows move,
scroll or change layout.

## Declaring a tutorial

Tutorials belong to the app's `.orion` file, next to its menus and forms:

```xml
<tutorials>
  <tutorial name="remove_blocks" form="main_window" title="Removing blocks">
    <step control="arrangement" part="selected-block"
          text="Click a block on the sheet to select it." />
    <step command="edit.delete" until="edit.delete"
          text="Click the trash button to remove the selected block." />
    <step control="arrangement" part="block" if="!defined(AX_PLATFORM_IOS)"
          text="You can also drag a block off the tracks, or right-click it." />
  </tutorial>
</tutorials>
```

| Attribute | Meaning |
|---|---|
| `name` | Remembered as `<prefix>.<name>` once finished or skipped |
| `form` | Form whose controls `control=` names |
| `title` | Optional heading over every step |
| `control` | A control of that form the step points at |
| `command` | A toolbar command the step points at (its button) |
| `part` | Passed to the target in `evGetTutorialAnchor` to narrow the pointer |
| `until` | A command that completes the step, as well as Next |
| `text` | The instruction; translated through the string catalog |
| `if` | Preprocessor condition, as on menu items |

`orionc` emits `<prefix>_<name>_tutorial`, a `tutorial_def_t`
(`orion/commctl/commctl.h`). Texts and titles are collected by
`orionc --strings` like any other `.orion` text.

## Running it

```c
tutorial_start(win, &groove_remove_blocks_tutorial);       // from a Help command
tutorial_start_once(win, &groove_remove_blocks_tutorial);  // unless finished or skipped before
```

`win` owns the tutorial: step targets are its controls and toolbar commands.
Report every command your dispatcher executes with `tutorial_command(id)` so
steps with `until=` advance however the command was issued (toolbar, menu,
accelerator or a gesture such as dragging a block off the sheet).

Finished and skipped tutorials are listed in `tutorials.txt` in the settings
directory, which all Orion applications share; orionc therefore names a
declared tutorial `<prefix>.<name>` (`groove.remove_blocks`).
`tutorial_set_seen(name, false)` shows a `_once` tutorial again, for a
"Reset tips" command.

## Pointing at a part

Without `part`, the step points at the whole control. With it, the control
receives `evGetTutorialAnchor` with a `tutorial_anchor_t`. Set `window` to a
descendant (a selected row, a card) or narrow `rect` (client coordinates) and
return `true`; return `false` to keep the whole control:

```c
case evGetTutorialAnchor: {
  tutorial_anchor_t *anchor = lparam;
  if (!anchor->part || strcmp(anchor->part, "selected-block")) return false;
  anchor->window = child_at(win, g_app->selected_clip);
  return anchor->window != NULL;
}
```

## Look

The bubble is a `SpeechBubble`; the ring is the `Tutorial::spot` box. Both come
from the style sheet:

```css
Tutorial        { --bubble-width: 300px; }
Tutorial::scrim { background: #0000008c; }   /* transparent turns the dimming off */
Tutorial::spot  { --outset: 4px; border: 3px solid var(--accent); border-radius: 8px; }
```

The scrim is drawn with `fill_rect_cutout()`, which fills a rectangle except a
rounded hole in one antialiased pass; the hole takes the ring's radius.
