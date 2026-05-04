---
layout: default
title: Dialogs & DDX
nav_order: 5
---

# Dialogs & Data Exchange

Orion dialogs follow the WinAPI `DialogBoxParam` / `EndDialog` pattern. This
page covers the full API: creating modal and modeless dialogs, the declarative
form system, and the DDX (Dialog Data Exchange) helpers that eliminate
boilerplate when reading from and writing to dialog controls.

---

## 1. Modal Dialogs

### `show_dialog` — raw modal dialog

```c
uint32_t show_dialog(
    const char  *title,  // title-bar text
    irect16_t const *frame, // MAKERECT(x, y, w, h) – logical pixels
    window_t    *parent, // owner, or NULL
    winproc_t    proc,   // dialog window procedure
    void        *param   // forwarded as lparam to evCreate
);
```

Blocks the caller until `end_dialog` closes the window.  Returns the code
passed to `end_dialog` (0 on X-button close).

### `show_dialog_from_form` — preferred API

```c
uint32_t show_dialog_from_form(
    form_def_t const *def,   // declarative layout (see Section 2)
    const char       *title, // overrides def->name; pass NULL to use def->name
    window_t         *parent,
    winproc_t         proc,
    void             *param
);
```

Auto-centers the dialog, adds the `WINDOW_DIALOG` flag, instantiates all child
controls from `def->children` **before** `evCreate` fires, then
runs the modal loop.  This is the preferred way to build any dialog with two
or more standard controls.

### `end_dialog` — close a modal dialog

```c
void end_dialog(window_t *win, uint32_t code);
```

`win` may be the dialog window itself or **any child** (e.g. a button).
`code` is returned to the `show_dialog[_from_form]` caller.

**Conventions:**

| Code | Meaning |
|------|---------|
| `0`  | Cancelled (user pressed Cancel or closed the X button) |
| `1`  | Accepted (user pressed OK) |
| Any other | Application-defined |

### Minimal example

```c
typedef struct { char path[512]; bool accepted; } open_dlg_t;

static result_t open_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
  open_dlg_t *s = (open_dlg_t *)win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      return true;
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *btn = (window_t *)lparam;
        if (btn->id == ID_OK) {
          s->accepted = true;
          end_dialog(win, 1);
        } else {
          end_dialog(win, 0);
        }
      }
      return true;
    default:
      return false;
  }
}

open_dlg_t state = {0};
show_dialog_from_form(&kMyForm, "Open", parent, open_proc, &state);
if (state.accepted) { /* … */ }
```

---

## 2. Declarative Forms

Any dialog with two or more standard controls should be laid out declaratively
using `form_ctrl_def_t` + `form_def_t`, analogous to WinAPI's `DLGTEMPLATE`.

### Control types

| Constant | Control |
|----------|---------|
| `FORM_CTRL_BUTTON` | Push button (`win_button`) |
| `FORM_CTRL_CHECKBOX` | Checkbox (`win_checkbox`) |
| `FORM_CTRL_LABEL` | Static text (`win_label`) |
| `FORM_CTRL_TEXTEDIT` | Single- or multi-line edit box (`win_textedit`) |
| `FORM_CTRL_LIST` | List control (`win_list`) |
| `FORM_CTRL_COMBOBOX` | Combo-box / dropdown (`win_combobox`) |

### Structs

```c
typedef struct {
  form_ctrl_type_t  type;   // FORM_CTRL_*
  uint32_t          id;     // numeric control ID; -1 for decorative controls
  irect16_t            frame;  // {x, y, w, h} in parent-client coordinates
  flags_t           flags;  // style flags (e.g. BUTTON_DEFAULT)
  const char       *text;   // initial caption / label text
  const char       *name;   // informational identifier (e.g. "edit_title")
} form_ctrl_def_t;

typedef struct {
  const char            *name;        // window title
  int                    w, h;        // client area dimensions
  flags_t                flags;       // WINDOW_* flags
  const form_ctrl_def_t *children;    // child control array (may be NULL)
  int                    child_count;
} form_def_t;
```

### Form definition example

```c
#define ID_NAME_EDIT   1
#define ID_OK_BTN      2
#define ID_CANCEL_BTN  3

static const form_ctrl_def_t kMyChildren[] = {
  { FORM_CTRL_LABEL,    -1,          {4,  8,  56, 13}, 0,             "Name:",   "lbl_name" },
  { FORM_CTRL_TEXTEDIT, ID_NAME_EDIT,{64, 8, 120, 13}, 0,             "",        "edit_name"},
  { FORM_CTRL_BUTTON,   ID_OK_BTN,   {60, 32, 40, 13}, BUTTON_DEFAULT,"OK",      "btn_ok"   },
  { FORM_CTRL_BUTTON,   ID_CANCEL_BTN,{104,32, 50, 13},0,             "Cancel",  "btn_cancel"},
};

static const form_def_t kMyForm = {
  .name        = "My Dialog",
  .w           = 164,
  .h           = 56,
  .flags       = 0,
  .children    = kMyChildren,
  .child_count = ARRAY_LEN(kMyChildren),
};
```

### Window procedure

Children are already created when `evCreate` fires; use
`get_window_item` / `set_window_item_text` to initialise them at runtime.

```c
static result_t my_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  my_state_t *s = (my_state_t *)win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      s = (my_state_t *)lparam;
      set_window_item_text(win, ID_NAME_EDIT, "%s", s->name); // pre-fill
      return true;
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == ID_OK_BTN) {
          /* read controls, validate, call end_dialog(win, 1) */
        }
        if (src->id == ID_CANCEL_BTN) end_dialog(win, 0);
      }
      return false;
    default:
      return false;
  }
}
```

**Key rules:**

- Do **not** call `create_window(…, win_button, …)` inside a form's
  `evCreate` when a `form_ctrl_def_t` entry can describe the same
  control.  Children declared in the form already exist when the message fires.
- Use `show_dialog_from_form()` for modal dialogs (handles centering +
  `WINDOW_DIALOG` flag automatically).
- Use `create_window_from_form()` for modeless panels / embedded sub-forms.
- When a modeless top-level form should open centered over another window,
  compute a frame rect and pass it through `center_window_rect()` instead of
  duplicating centering math locally.

```c
irect16_t wr = {0, 0, kMyForm.width, kMyForm.height};
adjust_window_rect(&wr, WINDOW_DIALOG | WINDOW_NORESIZE);
wr = center_window_rect(wr, parent);

window_t *win = create_window_from_form(&kMyForm, wr.x, wr.y,
                                        NULL, my_proc, 0, state);
```

---

## 3. Dialog Data Exchange (DDX)

Reading every control individually in the OK handler and writing them back in
`evCreate` produces identical boilerplate for every dialog.  DDX
replaces this with a **static binding table** that maps control IDs to struct
fields by offset, analogous to MFC's `DoDataExchange`.

### API

```c
// Binding type
typedef enum {
  BIND_STRING,    // char[] field ↔ text-edit text
  BIND_INT_COMBO, // int field   ↔ combo-box selection index
  BIND_INT,       // int field   ↔ text-edit decimal number
} bind_type_t;

// One entry in the binding table
typedef struct {
  uint32_t    ctrl_id; // numeric child control ID
  bind_type_t type;    // transfer type (BIND_*)
  size_t      offset;  // offsetof(state_t, field)
  size_t      size;    // for BIND_STRING: sizeof the char[] field; else 0
  uint16_t    command; // optional evCommand notification (HIWORD); 0 = any
} ctrl_binding_t;

// Populate controls from state (call from evCreate).
void dialog_push(window_t *win, const void *state,
                 const ctrl_binding_t *b, int n);

// Read controls into state (call before accept / end_dialog).
void dialog_pull(window_t *win, void *state,
                 const ctrl_binding_t *b, int n);

// Read only bindings listening to a specific evCommand notification.
int dialog_pull_command(window_t *win, void *state,
                        const ctrl_binding_t *b, int n,
                        uint16_t command);
```

### Helper macros

```c
// Number of elements in a statically-sized array.
ARRAY_LEN(arr)

// sizeof a named struct field — avoids a dummy pointer cast.
sizeof_field(type, field)
```

### Transfer types

| `bind_type_t` | Control | State field | Notes |
|---------------|---------|-------------|-------|
| `BIND_STRING` | `FORM_CTRL_TEXTEDIT` | `char[]` | `size` must equal `sizeof` the array |
| `BIND_INT_COMBO` | `FORM_CTRL_COMBOBOX` | `int` | Selection index (0-based); set `size = 0` |
| `BIND_INT` | `FORM_CTRL_TEXTEDIT` | `int` | Decimal text → `atoi`; set `size = 0` |

### Complete example — task edit dialog

```c
// ── State struct ─────────────────────────────────────────────────────
typedef struct {
  task_t *task;       // NULL = create, non-NULL = edit
  bool    accepted;
  char    title[128];
  char    desc[512];
  int     priority;   // PRIORITY_NORMAL etc.
  int     status;     // STATUS_TODO etc.
} task_dlg_t;

// ── Binding table ────────────────────────────────────────────────────
static const ctrl_binding_t k_bindings[] = {
  { ID_TITLE_EDIT,    BIND_STRING,    offsetof(task_dlg_t, title),    sizeof_field(task_dlg_t, title) },
  { ID_DESC_EDIT,     BIND_STRING,    offsetof(task_dlg_t, desc),     sizeof_field(task_dlg_t, desc)  },
  { ID_PRIORITY_COMBO,BIND_INT_COMBO, offsetof(task_dlg_t, priority), 0 },
  { ID_STATUS_COMBO,  BIND_INT_COMBO, offsetof(task_dlg_t, status),   0 },
};

// ── Window procedure ─────────────────────────────────────────────────
static result_t task_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  task_dlg_t *s = (task_dlg_t *)win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = s = (task_dlg_t *)lparam;
      populate_combo(win, ID_PRIORITY_COMBO);  // add combo items first
      populate_combo(win, ID_STATUS_COMBO);
      if (s->task) {
        // Copy existing values into state so dialog_push can show them.
        strncpy(s->title, s->task->title, sizeof(s->title) - 1);
        s->priority = (int)s->task->priority;
        s->status   = (int)s->task->status;
      }
      dialog_push(win, s, k_bindings, ARRAY_LEN(k_bindings));
      return true;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == ID_OK) {
          // Validate first
          window_t *et = get_window_item(win, ID_TITLE_EDIT);
          if (!et || et->title[0] == '\0') {
            message_box(win, "Title is required.", "Validation", MB_OK);
            return true;
          }
          dialog_pull(win, s, k_bindings, ARRAY_LEN(k_bindings));
          if (s->priority < 0) s->priority = PRIORITY_NORMAL;
          if (s->status   < 0) s->status   = STATUS_TODO;
          s->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == ID_CANCEL) { end_dialog(win, 0); return true; }
      }
      return false;
    default:
      return false;
  }
}
```

### When to use DDX vs manual reads

| Scenario | Recommendation |
|----------|---------------|
| Standard text/int/combo fields with a 1-to-1 field mapping | Use `dialog_push` + `dialog_pull` with a binding table |
| Custom validation before accepting (e.g. range checks, format checks) | Validate manually **after** `dialog_pull` |
| Fields that need computed / formatted values (e.g. `uint32_t` as decimal string) | Handle manually; omit from the binding table |
| Checkboxes (bool field from `win->value`) | Handle manually (`aa->value = st->flag`) |
| Read-only info labels | No binding needed; set text in `evCreate` directly |

### Mixing DDX with manual handling

DDX covers the common cases.  Controls whose logic falls outside the three
transfer types are simply omitted from the binding table and handled the
traditional way.  The binding table and manual code can coexist freely:

```c
case evCreate:
  // ... populate combos, etc. ...
  dialog_push(win, s, k_bindings, ARRAY_LEN(k_bindings));  // handles title/desc/combos
  if (s->due_date) {                                        // manual: uint32_t → string
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", s->due_date);
    set_window_item_text(win, ID_DUE_EDIT, "%s", buf);
  }
  return true;

case evCommand:
  if (src->id == ID_OK) {
    dialog_pull(win, s, k_bindings, ARRAY_LEN(k_bindings));  // handles title/desc/combos
    // Manual: parse uint32_t from the due-date field
    window_t *edue = get_window_item(win, ID_DUE_EDIT);
    if (edue && edue->title[0] != '\0') {
      char *endp;
      s->due_date = (uint32_t)strtoul(edue->title, &endp, 10);
      if (*endp != '\0') { /* show validation error */ return true; }
    }
    s->accepted = true;
    end_dialog(win, 1);
    return true;
  }
```

---

## 4. Reference

### Functions

| Function | Description |
|----------|-------------|
| `show_dialog(title, frame, parent, proc, param)` | Show a raw modal dialog |
| `show_dialog_from_form(def, title, parent, proc, param)` | Show a form-based modal dialog (preferred) |
| `create_window_from_form(def, x, y, parent, proc, param)` | Instantiate a form as a modeless window |
| `end_dialog(win, code)` | Close the nearest dialog ancestor and return a result code |
| `dialog_push(win, state, bindings, n)` | Copy state fields → dialog controls |
| `dialog_pull(win, state, bindings, n)` | Copy dialog controls → state fields |

### Macros

| Macro | Description |
|-------|-------------|
| `ARRAY_LEN(arr)` | Number of elements in a static array |
| `sizeof_field(type, field)` | `sizeof` a named struct member — use as `size` in `BIND_STRING` entries |
| `offsetof(type, field)` | Byte offset of a struct member — use as `offset` in all binding entries |
