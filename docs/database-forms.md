---
layout: default
title: Database Forms
nav_order: 8
---

# Database Forms: Zero-Boilerplate CRUD Dialogs
{: .no_toc }

**The Problem:** Traditional GUI database code is 90% boilerplate. For every simple edit dialog, you write the same pattern: create controls, wire up events, fetch the record, populate 20 fields manually, validate input, extract values back out, build an UPDATE statement, handle errors. Repeat for every table in your schema.

**The Solution:** Declare your form structure once in XML with `field="db.table.field"` attributes. Orion generates all the bindings, handles fetching/saving automatically, and gives you a single function call: `show_db_dialog()`. That's it.

---

## Table of Contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Quick Example: From 200 Lines to 20

### The Old Way (Manual Bindings)

```c
// Declare the dialog structure
typedef struct {
  int author_id;
  char name[64];
  char avatar[256];
} author_dlg_state_t;

// Fetch record from database
static void fetch_author(database_t *db, int id, author_dlg_state_t *state) {
  author_t *record = (author_t *)send_db_message(db, dbFind, 
                                                  MAKEDWORD(TABLE_AUTHORS, 0), 
                                                  (void *)(intptr_t)id);
  if (record) {
    state->author_id = record->id;
    strncpy(state->name, record->name, sizeof(state->name));
    strncpy(state->avatar, record->avatar, sizeof(state->avatar));
  }
}

// Populate dialog controls
static void populate_dialog(window_t *dlg, const author_dlg_state_t *state) {
  set_window_item_text(dlg, ID_NAME, state->name);
  set_window_item_text(dlg, ID_AVATAR, state->avatar);
}

// Extract values from controls
static void extract_values(window_t *dlg, author_dlg_state_t *state) {
  get_window_item_text(dlg, ID_NAME, state->name, sizeof(state->name));
  get_window_item_text(dlg, ID_AVATAR, state->avatar, sizeof(state->avatar));
}

// Save back to database
static void save_author(database_t *db, const author_dlg_state_t *state) {
  author_t record = {
    .id = state->author_id,
    .name = {0},
    .avatar = {0}
  };
  strncpy(record.name, state->name, sizeof(record.name));
  strncpy(record.avatar, state->avatar, sizeof(record.avatar));
  
  if (state->author_id > 0) {
    send_db_message(db, dbUpdate, MAKEDWORD(TABLE_AUTHORS, state->author_id), &record);
  } else {
    send_db_message(db, dbInsert, TABLE_AUTHORS, &record);
  }
}

// Window procedure
static result_t author_dlg_proc(window_t *win, uint32_t msg, 
                                uint32_t wparam, void *lparam) {
  author_dlg_state_t *state = (author_dlg_state_t *)win->userdata;
  
  switch (msg) {
    case evCreate:
      state = (author_dlg_state_t *)lparam;
      win->userdata = state;
      populate_dialog(win, state);
      return true;
      
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == ID_OK) {
          extract_values(win, state);
          save_author(state->db, state);
          end_dialog(win, 1);
          return true;
        }
        if (src->id == ID_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;
      
    default:
      return false;
  }
}

// Application code to show the dialog
void edit_author(window_t *parent, database_t *db, int author_id) {
  author_dlg_state_t state = {0};
  state.db = db;
  
  if (author_id > 0) {
    fetch_author(db, author_id, &state);
  }
  
  // Still need to manually define form structure...
  show_dialog_from_form(&author_form, "Edit Author", parent, 
                        author_dlg_proc, &state);
}
```

**Lines of code:** ~150 (plus form definition)  
**Potential bugs:** 10+ (field mismatches, buffer overflows, memory leaks, null checks)  
**Time to implement:** 20-30 minutes per table

---

### The Orion Way (Declarative)

**Step 1: Define the form in XML (edit_author.orion)**

```xml
<form name="edit_author" width="300" height="100">
  <grid spacing="4">
    <column width="60">
      <label text="Name:" />
      <label text="Avatar:" />
    </column>
    <column>
      <textedit field="db.authors.name" />
      <textedit field="db.authors.avatar" />
    </column>
  </grid>
  <separator />
  <stack orientation="horizontal" spacing="6">
    <space />
    <button value="1" text="OK" flags="BUTTON_DEFAULT" />
    <button value="2" text="Cancel" />
  </stack>
</form>
```

**Step 2: Generate the code**

```bash
$ orionc --input forms.orion --output forms.h --prefix myapp
```

**Step 3: Use it**

```c
#include "forms.h"

void edit_author(window_t *parent, int author_id) {
  show_db_dialog(&myapp_edit_author_form, "Edit Author", 
                 parent, author_id);
}
```

**Lines of code:** 3 (plus 15 lines of XML)  
**Potential bugs:** 0 (type-safe, generated code)  
**Time to implement:** 2 minutes

---

## What Gets Generated Automatically

When you add `field="db.authors.name"` to a control, orionc generates:

### 1. Control ID Constants

```c
#define ID_EDIT_AUTHOR_NAME    100
#define ID_EDIT_AUTHOR_AVATAR  101
#define ID_OK                  1
#define ID_CANCEL              2
```

### 2. Binding Array with Correct Types

```c
static const ctrl_binding_t myapp_edit_author_bindings[] = {
  { 
    .ctrl_id = ID_EDIT_AUTHOR_NAME,
    .command = 0,  // Any notification
    .getter = edGetText,
    .offset = offsetof(db_authors_t, name),
    .wparam = sizeof(((db_authors_t *)0)->name),
    .push = NULL,  // Message-based (no custom callback needed)
    .pull = NULL
  },
  { 
    .ctrl_id = ID_EDIT_AUTHOR_AVATAR,
    .command = 0,
    .getter = edGetText,
    .offset = offsetof(db_authors_t, avatar),
    .wparam = sizeof(((db_authors_t *)0)->avatar),
    .push = NULL,
    .pull = NULL
  },
};
```

### 3. Complete Form Definition

```c
static const form_def_t myapp_edit_author_form = {
  .name = "Edit Author",
  .width = 300,
  .height = 100,
  .children = myapp_edit_author_children,
  .child_count = 8,
  
  // DDX bindings
  .bindings = myapp_edit_author_bindings,
  .binding_count = 2,
  .ok_id = 1,
  .cancel_id = 2,
  
  // Database metadata
  .db_name = "db",
  .db_table = "authors",
  .db_table_id = TABLE_AUTHORS,
  .db_fields = authors_fields,
  .db_field_count = 3,
};
```

---

## Full Path Syntax: Multi-Database Support

Orion uses **explicit database scoping** with the format: `database.table.field`

### Why Full Paths?

**Problem:** In complex applications, you might have multiple databases:
```c
database_t *main_db;      // Primary application data
database_t *cache_db;     // Temporary caching layer
database_t *auth_db;      // User authentication/authorization
```

**Old approach:** Implicit database binding leads to ambiguity:
```xml
<!-- Which database does "users" come from? -->
<textedit field="users.name" />
```

**Orion solution:** Explicit database instance in every path:
```xml
<!-- Crystal clear: auth database, users table, name field -->
<textedit field="auth_db.users.name" />
<textedit field="cache_db.sessions.token" />
<textedit field="main_db.posts.title" />
```

### Path Format Reference

| Path | Parts | Usage |
|------|-------|-------|
| `db.posts` | 2 | Table reference for list controls (`source=`) |
| `db.posts.title` | 3 | Field reference for input controls (`field=`) |

### Attribute Conventions

| Attribute | Control Types | Example |
|-----------|---------------|---------|
| `source=` | List controls (tableview, reportview, combobox) | `<tableview source="db.posts" />` |
| `field=` | Input controls (textedit, checkbox, slider) | `<textedit field="db.posts.title" />` |
| `action=` | Action triggers (buttons, menu items) | `<button action="db.posts.insert" text="Save" />` |

---

## Control Type → Binding Rules

Orionc automatically selects the correct getter message and wparam based on control type:

| Control | Getter Message | wparam | Usage |
|---------|---------------|--------|-------|
| `textedit` | `edGetText` | Buffer size (`sizeof(field)`) | Single-line text input |
| `multiedit` | `edGetText` | Buffer size | Multi-line text input |
| `combobox` | `cbGetCurrentSelection` | `-1` (no default) | Dropdown selection (stores integer ID) |
| `checkbox` | `chkIsChecked` | `0` | Boolean/toggle control |
| `slider` | `slGetValue` | `0` | Integer value slider |

**Record Type Derivation:**
- Table name `"posts"` → Record type `db_posts_t`
- Table name `"authors"` → Record type `db_authors_t`
- The compiler generates `offsetof(db_posts_t, title)` automatically

---

## How show_db_dialog() Works

When you call:
```c
show_db_dialog(&form, "Edit Author", parent, author_id);
```

The framework executes this sequence:

### 1. Allocate Record Buffer

```c
// Allocate memory for the record based on table metadata
void *record_buf = calloc(1, record_size);
```

### 2. Fetch Existing Record (if record_id > 0)

```c
if (record_id > 0) {
  // Send dbFind message to database
  void *record = send_db_message(db, dbFind, 
                                  MAKEDWORD(table_id, 0), 
                                  (void *)(intptr_t)record_id);
  if (record) {
    memcpy(record_buf, record, record_size);
  }
}
```

### 3. Push Values to Controls (DDX Push)

```c
// For each binding in the array:
for (int i = 0; i < binding_count; i++) {
  const ctrl_binding_t *b = &bindings[i];
  void *field_ptr = (char *)record_buf + b->offset;
  
  // Use message-based push
  if (b->getter == edGetText) {
    set_window_item_text(dlg, b->ctrl_id, (const char *)field_ptr);
  } else if (b->getter == cbGetCurrentSelection) {
    set_window_item_int(dlg, b->ctrl_id, *(int *)field_ptr);
  }
  // ... etc for other control types
}
```

### 4. Show Modal Dialog

```c
// Standard modal dialog loop
uint32_t result = show_dialog_from_form(form, title, parent, 
                                        dialog_ddx_proc, &ctx);
```

### 5. Pull Values from Controls (on OK)

```c
if (result == 1) {  // OK button clicked
  for (int i = 0; i < binding_count; i++) {
    const ctrl_binding_t *b = &bindings[i];
    void *field_ptr = (char *)record_buf + b->offset;
    
    // Use message-based pull
    if (b->getter == edGetText) {
      get_window_item_text(dlg, b->ctrl_id, 
                          (char *)field_ptr, b->wparam);
    } else if (b->getter == cbGetCurrentSelection) {
      *(int *)field_ptr = get_window_item_int(dlg, b->ctrl_id);
    }
    // ... etc
  }
}
```

### 6. Save to Database

```c
if (result == 1) {
  if (record_id > 0) {
    // UPDATE existing record
    send_db_message(db, dbUpdate, 
                    MAKEDWORD(table_id, record_id), 
                    record_buf);
  } else {
    // INSERT new record
    send_db_message(db, dbInsert, table_id, record_buf);
  }
}
```

### 7. Cleanup

```c
free(record_buf);
```

**All of this happens automatically.** You write 1 function call.

---

## Real-World Example: Social Feed Application

### Database Schema

```xml
<database name="db" class="SimpleXMLDatabase">
  <table name="authors">
    <field name="id" type="integer" key="YES"/>
    <field name="name" type="string" length="64"/>
    <field name="avatar" type="string" length="256"/>
  </table>
  
  <table name="posts">
    <field name="id" type="integer" key="YES"/>
    <field name="author_id" type="integer" relation="authors.id"/>
    <field name="title" type="string" length="256"/>
    <field name="body" type="string" length="2048"/>
    <field name="like_count" type="integer"/>
  </table>
  
  <table name="comments">
    <field name="id" type="integer" key="YES"/>
    <field name="post_id" type="integer" relation="posts.id"/>
    <field name="author_id" type="integer" relation="authors.id"/>
    <field name="text" type="string" length="1024"/>
  </table>
</database>
```

### Form Definitions

**New Post Dialog:**
```xml
<form name="new_post" width="272" height="250">
  <grid spacing="4">
    <column width="56">
      <label text="Author:" />
      <label text="Title:" />
      <label text="Body:" />
    </column>
    <column>
      <combobox field="db.posts.author_id" source="db.authors" 
                display="name" value="id" />
      <textedit field="db.posts.title" />
      <multiedit field="db.posts.body" flags="WINDOW_FLEXSPACE" />
    </column>
  </grid>
  <separator />
  <stack orientation="horizontal" spacing="6">
    <space />
    <button value="1" text="Post" flags="BUTTON_DEFAULT" />
    <button value="2" text="Cancel" />
  </stack>
</form>
```

**New Comment Dialog:**
```xml
<form name="new_comment" width="260">
  <stack spacing="6">
    <stack orientation="horizontal" spacing="4">
      <label text="Author:" />
      <combobox field="db.comments.author_id" source="db.authors" 
                display="name" value="id" />
    </stack>
    
    <stack orientation="horizontal" spacing="4">
      <label text="Text:" />
      <textedit field="db.comments.text" flags="WINDOW_FLEXSPACE" />
    </stack>
  </stack>
  
  <separator />
  <stack orientation="horizontal" spacing="6">
    <space />
    <button value="1" text="Submit" flags="BUTTON_DEFAULT" />
    <button value="2" text="Cancel" />
  </stack>
</form>
```

### Generated Code

After running orionc, you get complete, type-safe binding arrays:

```c
// New post form: 3 bindings
static const ctrl_binding_t socialfeed_new_post_bindings[] = {
  { ID_NEW_POST_AUTHOR, 0, cbGetCurrentSelection, 
    offsetof(db_posts_t, author_id), -1, NULL, NULL },
  { ID_NEW_POST_TITLE, 0, edGetText, 
    offsetof(db_posts_t, title), 
    sizeof(((db_posts_t *)0)->title), NULL, NULL },
  { ID_NEW_POST_BODY, 0, edGetText, 
    offsetof(db_posts_t, body), 
    sizeof(((db_posts_t *)0)->body), NULL, NULL },
};

// New comment form: 2 bindings
static const ctrl_binding_t socialfeed_new_comment_bindings[] = {
  { ID_NEW_COMMENT_AUTHOR, 0, cbGetCurrentSelection, 
    offsetof(db_comments_t, author_id), -1, NULL, NULL },
  { ID_NEW_COMMENT_TEXT, 0, edGetText, 
    offsetof(db_comments_t, text), 
    sizeof(((db_comments_t *)0)->text), NULL, NULL },
};
```

### Application Code

**Creating a new post:**
```c
void handle_new_post(window_t *parent) {
  // That's it. One line. Auto-fetch, auto-populate, auto-save.
  show_db_dialog(&socialfeed_new_post_form, "New Post", 
                 parent, 0);  // 0 = INSERT mode
}
```

**Editing an existing post:**
```c
void handle_edit_post(window_t *parent, int post_id) {
  // Same function, different record_id = UPDATE mode
  show_db_dialog(&socialfeed_new_post_form, "Edit Post", 
                 parent, post_id);
}
```

**Adding a comment:**
```c
void handle_new_comment(window_t *parent, int post_id) {
  show_db_dialog_ex(&socialfeed_new_comment_form, "New Comment",
                    parent, 0, "post_id", post_id);
}
```

---

## Benefits Over Traditional Approaches

### 1. **Massive Code Reduction**

**Traditional MFC/WinForms CRUD:**
- Form designer generates partial code
- You write manual databinding code (~100 lines per form)
- Field-by-field validation
- Manual SQL generation or ORM configuration
- Error handling for each field

**Orion:**
- XML form definition (15-20 lines)
- Zero manual binding code
- Type-safe at compile time
- One function call to show/save

### 2. **Zero Boilerplate**

Every CRUD dialog needs the same pattern:
- Fetch record
- Populate UI
- Validate input
- Extract values
- Save to database

With Orion, you write this logic **zero times**. The framework handles it universally.

### 3. **Type Safety**

Bindings use `offsetof()` and `sizeof()`, so:
- **Compile-time errors** if field names don't match
- **No runtime crashes** from wrong field types
- **No buffer overflows** - sizes calculated automatically
- **Refactoring-safe** - rename a field, recompile, done

### 4. **Database Agnostic**

The binding system doesn't care about your database implementation:
- SQL database? ✓
- XML files? ✓
- JSON API? ✓
- In-memory store? ✓

As long as it implements `dbFind`, `dbInsert`, `dbUpdate` messages, it works.

### 5. **Consistency**

Every database dialog in your application:
- Uses the same pattern
- Has the same UX behavior
- Handles errors the same way
- Validates the same way

No "this developer did it differently" problems.

### 6. **Rapid Prototyping**

Need to add a new table? Add 3 things:
1. Table definition in XML schema
2. Form definition with field= attributes
3. One `show_db_dialog()` call

Time: **2-5 minutes per table**

---

## Advanced Usage

### Mixing Auto-Bindings with Custom Logic

Sometimes you need custom behavior. No problem - bindings are optional:

```c
static result_t custom_post_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  db_dlg_ctx_t *ctx = (db_dlg_ctx_t *)win->userdata;
  
  switch (msg) {
    case evCreate:
      // Auto-bindings already populated the form
      // Add custom initialization
      enable_window_item(win, ID_PUBLISH_BTN, 
                        user_has_permission(PERM_PUBLISH));
      return true;
      
    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        
        // Custom validation before save
        if (src->id == ctx->ok_id) {
          char title[256];
          get_window_item_text(win, ID_TITLE, title, sizeof(title));
          
          if (strlen(title) < 3) {
            show_message_box(win, "Error", 
                           "Title must be at least 3 characters",
                           MB_OK | MB_ICONERROR);
            return true;  // Block the save
          }
        }
      }
      break;
  }
  
  // Let default handler do the rest
  return false;
}

// Use custom proc with auto-bindings
show_db_dialog_ex(&form, "New Post", parent, db, 0, custom_post_proc);
```

### Read-Only Fields

Mark fields read-only in the form:
```xml
<textedit field="db.posts.created_at" flags="WINDOW_READONLY" />
```

The binding still works (field is populated), but user can't edit it.

### Conditional Display

Hide fields based on record state:
```c
case evCreate:
  if (is_new_record) {
    hide_window_item(win, ID_CREATED_AT);
    hide_window_item(win, ID_MODIFIED_AT);
  }
  return true;
```

### Validation

Add validation in window proc before accepting OK:
```c
case evCommand:
  if (src->id == ID_OK) {
    // Custom validation
    if (!validate_email(email_field)) {
      show_error("Invalid email address");
      return true;  // Block dialog close
    }
  }
  break;
```

---

## Migration from Manual Bindings

Already have manual binding code? Easy migration path:

### Step 1: Add field= Attributes

```xml
<!-- Before: manual bindings -->
<textedit name="title" />

<!-- After: declarative -->
<textedit name="title" field="db.posts.title" />
```

### Step 2: Remove Manual Binding Code

Delete your manual binding array - orionc generates it now.

### Step 3: Switch to show_db_dialog()

```c
// Before
show_dialog_from_form(&form, title, parent, custom_proc, &state);

// After
show_db_dialog(&form, title, parent, record_id);
```

### Step 4: Cleanup

Remove old functions:
- `fetch_record()`
- `populate_dialog()`
- `extract_values()`
- `save_record()`

**Time saved per table:** 15-20 minutes  
**Bugs eliminated:** 5-10 per table  
**Code removed:** 100-150 lines per table

---

## Troubleshooting

### "Undefined symbol: db_posts_t"

**Cause:** Record type doesn't exist or isn't visible to the form code.

**Fix:** Ensure your database schema generates the record types:
```c
// Generated from <table name="posts">
typedef struct {
  int id;
  int author_id;
  char title[256];
  char body[2048];
  int like_count;
} db_posts_t;
```

### "Control not updating"

**Cause:** Wrong getter message for control type.

**Fix:** Check the generated binding - orionc auto-selects based on class:
```c
// textedit → edGetText ✓
// combobox → cbGetCurrentSelection ✓
// checkbox → chkIsChecked ✓
```

### "Buffer overflow in text field"

**Cause:** Database field size doesn't match record struct size.

**Fix:** Regenerate record types from updated schema:
```xml
<!-- Schema must match record type -->
<field name="title" type="string" length="256"/>
```

```c
// Generated struct must match
char title[256];  // Same size
```

### "Record not saving"

**Cause:** Database doesn't handle dbInsert/dbUpdate messages.

**Fix:** Implement the database message handler:
```c
case dbInsert:
  table_id = wparam;
  record = lparam;
  // Insert record into table
  return (lresult_t)inserted_record;

case dbUpdate:
  table_id = LOWORD(wparam);
  record_id = HIWORD(wparam);
  record = lparam;
  // Update record in table
  return 1;  // Success
```

---

## Performance Notes

**Memory:**
- One record buffer allocated per dialog (typically 1-4KB)
- Freed when dialog closes
- No persistent allocations

**Speed:**
- Form generation: Compile-time only
- Dialog show: < 1ms (single dbFind message)
- Save: < 1ms (single dbInsert/dbUpdate message)
- **No runtime reflection or parsing**

**Scaling:**
- Handles tables with 50+ fields efficiently
- Binding lookup is O(1) (array index)
- No performance difference between 5 fields and 50 fields

---

## Comparison with Other Frameworks

### vs. Qt Model/View

**Qt:**
```cpp
// Create model
QSqlTableModel *model = new QSqlTableModel(this, db);
model->setTable("posts");
model->setEditStrategy(QSqlTableModel::OnManualSubmit);
model->select();

// Create mapper
QDataWidgetMapper *mapper = new QDataWidgetMapper(this);
mapper->setModel(model);
mapper->addMapping(ui->titleEdit, model->fieldIndex("title"));
mapper->addMapping(ui->bodyEdit, model->fieldIndex("body"));
mapper->toFirst();

// Manual save
connect(ui->saveButton, &QPushButton::clicked, [=]() {
  mapper->submit();
  model->submitAll();
});
```

**Orion:**
```xml
<textedit field="db.posts.title" />
<textedit field="db.posts.body" />
```
```c
show_db_dialog(&form, "Edit", parent, post_id);
```

### vs. WinForms BindingSource

**WinForms:**
```csharp
// Setup binding source
var bindingSource = new BindingSource();
bindingSource.DataSource = typeof(Post);

// Bind each control
titleTextBox.DataBindings.Add("Text", bindingSource, "Title");
bodyTextBox.DataBindings.Add("Text", bindingSource, "Body");

// Load data
var post = db.Posts.Find(postId);
bindingSource.DataSource = post;

// Save
db.SaveChanges();
```

**Orion:**
```c
show_db_dialog(&form, "Edit", parent, post_id);
```

### vs. Raw SQL

**Raw SQL (C):**
```c
// 80+ lines of:
// - SQL string building
// - Parameter binding
// - Result set extraction
// - Error handling
// - Memory management
// - Type conversions
```

**Orion:**
```c
show_db_dialog(&form, "Edit", parent, post_id);
```

---

## Philosophy

Orion's database forms follow three principles:

### 1. Declaration over Implementation

You declare **what** you want (field bindings), not **how** to do it (fetch/save logic).

### 2. Convention over Configuration

Smart defaults based on control types. Override only when needed.

### 3. Compile-Time over Runtime

All bindings generated at compile time. No runtime reflection, parsing, or string manipulation.

---

## Summary

**What you write:**
```xml
<textedit field="db.posts.title" />
```

**What you get:**
- Type-safe binding with offsetof()
- Automatic size calculation with sizeof()
- Correct getter message (edGetText)
- Auto-fetch from database
- Auto-save to database
- Full DDX push/pull support
- Zero manual code

**Result:** 95% less boilerplate, 100% type-safe, infinitely maintainable.

**This is database forms done right.**

---

## See Also

- [Database Schema](database-codegen.md) - Table definitions and code generation
- [Dialogs](dialogs.md) - Modal dialog system overview
- [Controls](controls.md) - Available control types and attributes
- [Messages](messages.md) - Window message reference

---

## Schema-Driven Databases (`db_schema_xml`)

For data that lives in XML, an app needs no adaptor at all. orionc emits a
`db_schema_def_t` for every `<database>` (`<prefix>_<db>_schema`) with record
sizes, field offsets, keys and joins, and the framework's `db_schema_xml`
class implements the whole message set from it:

```c
DB_CLASS(db_schema_xml);
database_t *ib = create_database_with_schema("ib", "db_schema_xml",
                                             &ledger_ib_schema, path);
register_database("ib", ib);
```

- `dbInsert` assigns the next key; records are individually allocated, so
  pointers stay valid until the record is deleted.
- `dbFetch` filters on any integer, boolean or string field by index;
  `dbFind` searches the key (index 0) or any other field.
- `dbSave` writes `<db><table><row .../></table></db>`; a missing file loads
  empty.
- `dbGetFieldText` formats fields for views: numbers and `type="date"` fields
  follow the active [string catalog](localization.md) (`16 656,00`,
  `10.03.2003` in Russian). A `relation=` field becomes a
  join named after it without `_id`, so columns can show `author.name` or
  nested paths such as `item.unit.name`. A relation may name another
  registered database: `relation="cfg.accounts.id"`.

TableView and ComboBox format cells through `db_get_field_text()`, which uses
`dbGetFieldText` when the database implements it and falls back to object
procs and field bindings otherwise.

A detail TableView follows its master's selection as soon as both are bound,
and `tvRefresh` keeps the current selection.

## Typed Bindings

orionc chooses the DDX exchange from the bound field's declared type:

| Control | Field type | Exchange |
|---|---|---|
| `TextBox` / `MultiEdit` | `string` | `edGetText` / `edSetText` |
| `TextBox` | `integer` | `ddx_push_int` / `ddx_pull_int` |
| `TextBox` | `double` | `ddx_push_double` / `ddx_pull_double` (catalog number format; also accepts `1234.50`) |
| `TextBox` | `date` | `ddx_push_date` / `ddx_pull_date` (stored `YYYY-MM-DD`, edited in the catalog's date format) |
| `ComboBox` with `source=` | `integer` | `ddx_push_combo_value` / `ddx_pull_combo_value` (record id, 0 = none) |
| `CheckBox` | `boolean` | `ddx_push_check` / `ddx_pull_check` |

Generated forms carry `db_record_size`, which `show_db_dialog` uses to size
and copy the record. `show_db_dialog_hook()` forwards `evCreate` and unhandled
messages to an app procedure, for dialogs that manage a detail table:

```c
static result_t document_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCreate) {
    send_message(get_window_item(win, ID_DOCUMENT_LINES), tvSetFilter,
                 DOC_LINES_DOCUMENT_ID_FIELD, (void *)(intptr_t)db_dialog_record_id(win));
    return true;
  }
  return false;
}
show_db_dialog_hook(&ledger_document_form, NULL, parent, doc_id, document_hook);
```

