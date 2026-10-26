---
layout: default
title: Lua Add-ons
nav_order: 15
has_children: true
---

# Lua Add-ons

An add-on is two files side by side, with no C: a `.orion` file declares its
forms and scratch tables, and a `.lua` file supplies the behaviour. A host
application opens one with `script_page_open()` and shows it like any dialog.
The model is lua-objc's. **A page is a request.** The host asks the page for
`data()` and shows it. A button's `action=` runs as a method of the page, with
the form's typed inputs. Then the host asks for `data()` again. There are no
bindings, observers or notifications.

```xml
<!-- roster.orion -->
<orion>
  <databases>
    <database name="roster" class="db_schema_xml">
      <table name="lines" transient="true">
        <field name="id" type="integer" key="YES" />
        <field name="name" type="string" length="32" />
        <field name="age" type="integer" />
      </table>
    </database>
  </databases>
  <forms>
    <form name="roster" title="Roster" width="400" height="300">
      <TextBox name="min_age" type="integer" />
      <Label name="summary" />
      <Label name="error" />
      <TableView name="lines" source="roster.lines">
        <Column field="name" title="Name" />
        <Column field="age" title="Age" />
      </TableView>
      <Button text="Filter" action="filter" />
      <Button text="Cancel" />
    </form>
  </forms>
</orion>
```

```lua
-- roster.lua
local Model = require("orion.model")
local People = Model:extend("club.people")

local Roster = {form = "roster", title = "Club roster"}

function Roster:init()
	self.minAge = 0
end

function Roster:data()
	local rows = {}
	for _, p in ipairs(People:select(function(p) return p.age >= self.minAge end, {order = "age desc"})) do
		table.insert(rows, {name = p.name, age = p.age})
	end
	return {values = {min_age = self.minAge, summary = #rows .. " people"}, lists = {lines = rows}}
end

function Roster:filter(values)
	self.minAge = values.min_age or 0
end

return Roster
```

The full example, with its tests, is `tests/fixtures/script/` and
`tests/script_test.c`.

## The page

The script returns its page table. The host reads these fields:

| Field | Meaning |
|---|---|
| `form` | The form to show; the first form of the `.orion` file when absent |
| `title` | The window title; the form's title when absent |
| `init(self)` | Runs once when the add-on opens; a refusal there stops the opening |
| `data(self)` | What the form shows now (below); called on opening and after every action |
| any other method | An action: `<Button action="name">` calls `self:name(values)` |

`data()` returns a table with any of the following:

| Key | Effect |
|---|---|
| `values = {name = value}` | Sets controls by `name=`. A text box shows text, a number or a date (as its `type=` says). A CheckBox takes a boolean. A ComboBox bound to a datasource takes a key; any other ComboBox takes a 1-based index. Any other control takes it as its caption. |
| `lists = {name = rows}` | Fills the TableView `name` with exactly these rows. Its `source=` must be a table of the add-on's own database. |
| `hidden = {name = true}` | Hides controls |
| `disabled = {name = true}` | Disables controls |
| `title = "..."` | Retitles the window |

An action receives `values`, the typed contents of every named input:

| Control | Value |
|---|---|
| TextBox, TextEdit, MultiEdit | A string; with `type="integer"` an integer, with `type="number"` a number, with `type="date"` an ISO date (`"2003-03-31"`); `nil` when empty or invalid. `decimals="N"` sets how a number shows. Numbers and dates use the catalogue's format, as typed fields do. |
| CheckBox | A boolean |
| ComboBox | The key of the selected record; for a plain list, the 1-based index |
| TableView | The selected row as a table, or `nil` |

`self.app.close(result)` closes the page once the action returns. The Cancel
button (`text="Cancel"` or `name="cancel"`) and the window's close box close it
without asking the script.

## Refusals and errors

An action refuses by calling `orion.fail("message")`, by returning
`nil, "message"`, or by passing a model's refusal through
`orion.assert(row:update{...})`. The page stays as it was and the message
shows: in a control named `error` when the form has one, otherwise in a
message box. A mistake in the script is a different matter. It writes the
traceback to stderr and shows "The add-on stopped with an error". The page
stays usable. Mistakes include calling a missing field, passing a wrong type or
naming an unknown control.

## orion.model

Records of any registered datasource, read and written through the same
messages C code sends: `dbFetch`, `dbFind`, `dbInsert`, `dbUpdate`, `dbDelete`
and change scopes. The datasource keeps its rules, so defaults, numbering and
delete rules apply to scripts as they do to forms.

```lua
local Model = require("orion.model")
local Counterparties, Counterparty = Model:extend("ib.counterparties", {
	constraints = {name = function(_, name) if name == "" then return "A counterparty needs a name." end end},
})
function Counterparties:named(text) return self:select(function(c) return c.name:find(text, 1, true) end) end
function Counterparty:label() return self.inn .. " " .. self.name end
```

| Call | Returns |
|---|---|
| `Model:extend("db.table", spec)` | The class (queries) and the row table (record methods). `spec.constraints` validates writes. Other spec fields become class constants. |
| `Class:all()` | Every row |
| `Class:find(key)`, `Class:find{field = value}` | The first matching row, or `nil` |
| `Class:select(where, {order = "date desc, number", limit = 10})` | Rows. `where` is `nil`, a table of equal fields, or a function of a row. |
| `Class:count(where)` | The number of matching rows |
| `Class:create(fields)` | The new row (defaults filled, key assigned), or `nil, message` |
| `Class:transaction(fn)` | `fn`'s results. Its changes stay together, or all are undone when it fails or refuses. |
| `row.field` | A field value: integer, number, boolean, or string (dates are ISO strings) |
| `row.alias` | The record a relation points at (`account_id` gives `row.account`) |
| `row:update(fields)` | The refreshed row, or `nil, message` |
| `row:delete()` | `true`, or `nil, message` when the record is in use |
| `row:text("item.unit.name")` | A field or join path as the application displays it |

A value the datasource cannot store is refused with a message, for example a
string longer than its field or text that is not a date. An unknown field or
a value of the wrong type is a mistake in the script and raises an error.

## orion and orion.db

[`orion`](lua/orion.md) (refusals, translation, number and date formats) and
[`orion.db`](lua/db.md) (the datasource messages orion.model builds on) are
C modules. Their references are generated from the same `.cgen` files as
their bindings.

## The sandbox

Each add-on gets its own Lua state with:

- base, `string`, `table`, `math`, `utf8`, `coroutine`;
- `os.time`, `os.date`, `os.clock`, `os.difftime`.

It has no `io`, `load`, `loadfile`, `dofile`, `string.dump` or native modules.
`require()` reaches three places: the add-on's own folder (`require("helpers")`
loads `helpers.lua` beside the script), Orion's modules in
`share/orion/lua`, and the modules the host registers. `print()` writes to
stderr with the add-on's name. Data reaches a script only through
datasources.

An add-on's databases live in memory. They are registered under their
declared names while the add-on is open, so a second add-on with the same
database name cannot open alongside it.

An add-on's catalogs live in a `locale/` folder beside it (`ru.xml`, `de.xml`),
made with `orionc --strings` from its `.orion` file plus its `orion.tr` texts
as `where="code"` entries. They are scoped: a form text translates in the forms
its `where=` names, and code texts only while the add-on's own code runs, so
an add-on cannot change the host's texts or its number and date formats
(`ui_add_scoped_string_catalog_dir()`).

## Hosting add-ons (C)

An add-on's `data()` can also provide `sprites = {mascot = {path = "assets/idle.png",
columns = 4, rows = 2, count = 8, interval = 125, loop = true, revision = 1}}`.
The named control must be a `SpriteButton`; the PNG path is relative to the
add-on directory. The host owns the bitmap and animation timer. Increment
`revision` to restart the same clip. `self.app.focus("command")` focuses a
named control after the action's data has rendered and layout has finished,
so an action can reveal a hidden panel before focusing its input.

Native hosts can use `script_page_create_hook(page, parent, hook)` for window
presentation, such as hit-testing the visible parts of a transparent companion.
The hook may consume ordinary messages; the page keeps its userdata and handles
its own lifecycle and sprite cleanup. `script_page_preferred_size(page)` returns
the dimensions declared in the form for fitting the presentation to a display.

```c
#include <orion/script/script.h>

script_register_module("ledger", luaopen_ledger);   // before opening pages
char error[512];
script_page_t *page = script_page_open("addons/penalties", error, sizeof(error));
if (page) {
  script_page_run(page, main_window);   // modal
  script_page_close(page);
} else {
  message_box(main_window, error, ui_tr("Add-on"), MB_OK);
}
```

`script_page_create()`, `script_page_perform()`, `script_page_message()` and
`script_page_control()` run a page without a modal loop, for tests and for
hosts that embed one.

## Bindings are generated

No Lua binding is written by hand. A module is declared in a `.cgen` file, in
the schema of orca's code generator: each function with its arguments, their
types and its results. `tools/cgen.c` writes the wrappers that check the
arguments and the `luaopen_` function that registers them. The Makefile
generates `build/generated/<dir>/<stem>_lua.h` for every `orion/*/*.cgen`
and `apps/*/*.cgen`, and the file implementing the module includes it at its
end. The header declares each bound C function, so an implementation whose
signature disagrees with the `.cgen` does not compile.

```xml
<!-- apps/ledger/ledger_addons.cgen -->
<module name="ledger" prefix="ledger_addon_">
  <functions>
    <function name="post" lua="true">                  <!-- int ledger_addon_post(lua_State *L, int id) -->
      <summary>Posts a document.</summary>
      <arg name="id" type="int">The document</arg>
      <returns type="nresults">true, or nil and a message</returns>
    </function>
    <function name="format_number" call="ui_format_number">  <!-- Lua: formatNumber -->
      <arg name="value" type="number"/>
      <arg name="decimals" type="int" default="2"/>
      <arg name="buf" type="string" output="char[64]"/>      <!-- a local buffer -->
      <arg name="size" type="size" bufsize="buf"/>           <!-- sizeof(buf) -->
      <returns type="string"/>
    </function>
  </functions>
</module>
```

```c
// ledger_addons.c
static int ledger_addon_post(lua_State *L, int id) { ... }
#include "build/generated/apps/ledger/ledger_addons_lua.h"   // defines luaopen_ledger
```

| Declaration | Meaning |
|---|---|
| `<module name prefix luaopen>` | `require(name)`; the open function is `luaopen`, else `luaopen_<name with _ for .>`; C functions are `prefix` + the function's name |
| `<function name export call lua>` | Lua name `export`, else the name in camelCase; `call` names another C function; `lua="true"` passes the state first |
| `<arg type>` | `int`, `number`, `bool`, `string`, or a stack index: `value`, `table`, `function` |
| `<arg optional default>` | `luaL_opt*`; `nil` gives `NULL` or 0 |
| `<arg output bufsize>` | A local buffer and its size, not Lua arguments |
| `<returns type>` | A pushed `int`, `number`, `bool` or `string` (`NULL` gives `nil`); `nresults` when the function pushes its own results; none for nothing |
| `on-luaopen`, `after-luaopen` | `void f(lua_State *)` called with the new module table on top, to add fields to it |

The generator also writes each framework module's reference: `make lua-docs`
regenerates `docs/lua/`, and `make test` fails while it is out of date.
