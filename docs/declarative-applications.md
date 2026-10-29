---
layout: default
title: Declarative applications
parent: Reference
nav_order: 20
---

# Declarative applications and Lua ownership

Orion applications compose native controls and datasources from `.orion` object
graphs. Lua supplies application policy where declarations stop being sufficient.
Lodge runs through the shared host without app-specific C source or bindings.
Ledger uses the same runtime for its Lua command controller and trusted assistant,
with a generated capability API over its existing native accounting/editor backend.

## Runtime, window and object lifetime

Use **one `script_runtime_t` per application/trust domain**, confined to the UI
thread. It owns the Lua VM; it is not a process-global singleton. Trusted windows
share core/native modules, models and plugin instances. Every window loads a fresh
controller object, retained by a Lua registry reference. Package directories get
separate `_ENV`, local `require` and module caches, so two packages can both have a
`helper.lua`. The shared Orion modules retain application-wide identity.

`lua_State *` identifies an interpreter thread; `lua_newthread` shares its VM and
is not an isolation boundary. A Lua userdata is an object managed by Lua, not a
VM. Orion's `window_t.userdata` instead points to native page state. That state
borrows the runtime and holds a controller reference. The generated window API
uses monotonically assigned handles, validates the owning VM and rejects stale
handles; it never exposes `window_t *` to Lua.

Close pages, release controllers, then close the runtime. Runtime close rejects
live controllers/pages. The application host then destroys its datasources and
resource library. Database observers are unsubscribed before teardown. Borrowed
resource libraries outlive all forms created from them.

Independent external Ledger add-ons retain isolated VMs. Configuration accounting
rules also retain their separate evaluator. Sharing a trusted UI VM does not turn
an add-on sandbox into a permissions boundary. Native modules operate on the
registered datasources the host exposes; malicious installed code is out of scope.

## An application without a C entry point

```xml
<orion>
  <include file="schema.orion"/>
  <application name="hotel" form="rooms"/>
  <forms>
    <form name="rooms" title="Rooms" width="600" height="400">
      <TableView name="records" source="hotel.rooms" form="room">
        <Column field="name" title="Name" width="0"/>
      </TableView>
      <Button text="Add" target="records" action="insert"/>
      <Button text="Edit" target="records" action="edit"/>
      <Button text="Delete" target="records" action="delete"/>
      <Button text="Fetch" target="records" action="fetch"/>
    </form>
    <form name="room" title="Room" width="420" height="180">
      <TextBox name="name" field="hotel.rooms.name"/>
      <Button text="Save" action="db.update" flags="BUTTON_DEFAULT"/>
      <Button text="Cancel"/>
    </form>
  </forms>
</orion>
```

`schema.orion` declares a `hotel` database with a `rooms` table. Persistent database
sources are filenames relative to the host's data directory. XML includes resolve
relative to their containing file, load without network access and have bounded
nesting. Both the runtime loader and `orionc` expand the same includes.

```sh
make share build/bin/orion-run
build/bin/orion-run path/to/app-folder --data /path/to/data
# Installed runtime (no compiler needed):
orion path/to/app-folder
```

The folder entry point is `application.orion`; it explicitly names the initial
form and optional model/controller modules. The host loads referenced `.orion`
includes and Lua modules, rather than executing every file in the directory.
`--app path/to/application.orion` remains supported. The runtime is compiled once;
editing or distributing a Lua application's folder requires no C compiler,
app-specific `main.c`, `.cgen` bindings or generated C schema. Lodge's generated
schema is used by its native tests, not by the folder launcher.

`make lodge` uses the common entry point automatically. Linux/Windows portable
bundles keep the application under `share/lodge` beside the shared runtime and
libraries. macOS bundles put these resources in `Contents/Resources/share/lodge`
with the existing `Contents/share` compatibility link; the executable discovers
them relative to itself, independent of the working directory. A generic host also
accepts `Contents/Resources/application.orion` for a flat application resource
folder. Persistent data and generated documents live in the writable user data
directory, never inside the installed application or signed bundle.

No Lua controller is needed for these CRUD windows. `TableView form=` opens a
native record editing context. Save applies the delta; Cancel discards it.
Schema defaults, relationships and delete rules stay in the datasource. Lodge's
rooms and guests forms are working examples. The Makefile also supplies the
shared launcher to apps without `main.c`, so `make lodge` remains available.

## DisplayGroups and selection bindings

A DisplayGroup adapts an existing table into an `objects` list and zero-or-one
`selection` table. The group owns the selection key; it borrows source records.

```xml
<display-groups>
  <DisplayGroup name="people" source="catalog.people"/>
</display-groups>
```

Bind a TableView to `people.objects`, a Label to `people.selection.name`, and an
ImageView to `people.selection.photo`. The page adapter translates TableView
selection notifications into `dbSelectRecord`. Labels and images update through
ordinary typed DDX bindings, with no Lua callback. ImageView owns the bitmap it
loads from the string path and preserves its aspect ratio. Empty selection clears
the bound controls. See `tests/fixtures/runtime/browser.orion`.

Source writes publish database notifications after the outermost successful
change scope. Reverted changes do not publish. The host refreshes open forms;
selection changes update bindings without refetching the list. Notifications are
currently coarse invalidations, not a property-level reactive dependency graph.
DisplayGroups currently provide list/selection adaptation and forward native
fetch filters; richer query specifications, independent sorting and paging remain
future extensions. They are application-scoped named objects in this version.

## Lua models and computed properties

Add `model="model"` on `<application>` to load `model.lua`. It returns an object
with optional methods:

```lua
local Model = {}
function Model:validate(tableName, fields, previous)
  if tableName == "rooms" and fields.capacity < 1 then
    return nil, require("orion").tr("Capacity must be positive.")
  end
  return true
end
function Model:compute(tableName, fields, property)
  if property == "balance" then return fields.total - fields.paid end
end
return Model
```

Declare computed display fields with `computed="balance"` in the schema. The
native model adapter calls Lua when a view asks for field text. These fields are
read-only through Lua writes and omitted from XML persistence. They are not yet
lazy properties in the Lua row API: use the domain function directly in Lua.
Validation runs for native inserts/updates as well as Lua writes. Editing-context
validation occurs when its delta is applied to the parent, so a codeless editor
cannot bypass hotel rules. Validators must be read-only and cannot yield.

Add `controller="reception"` only to forms that need custom policy. The controller
receives typed field values and selected records, as in existing script pages.
`orion.application.save(database)`, `open(form)` and `write(filename, text)` are
shared host capabilities. Save failures are refusals; reservation workflows raise
them inside a transaction to roll back the action. The host blocks closing a
window if saving fails. Writes are atomic document replacement within the data
directory. File basenames cannot escape that directory.

## Generate the native boundary

Declare capabilities in `.cgen`, implement ordinary typed C functions, and include
the generated header. `cgen` emits argument validation, marshaling, return handling,
module registration and Markdown reference from that contract. The build derives
core module preloads and LuaLS declarations (`build/generated/lua`) from the same
contracts through `tools/script_metadata.py`; do not maintain a parallel registry.
Use `export=` when an exact Lua method spelling is required; default exports use
camelCase. App-specific declarations such as Ledger's `controller.cgen` use the
same generator. App module registration grants that host's capabilities explicitly.

A window action can name a module capability directly:

```xml
<Button text="Rooms" target="orion.application" action="open" argument="rooms"/>
```

The runtime resolves the module/method, and the generated wrapper checks argument
types. This initial action syntax passes zero or one string argument. General
expression evaluation, arbitrary C function calls, async tasks, undo coordination
and hot reload are not part of this contract.

## Ledger migration boundary

Ledger's former C command switch, document-kind selection, private month-end
calendar, status aggregation and command policy now live in
`apps/ledger/share/controller.lua`. Declarative screens remain in `ledger.orion`.
Its native adapter exposes focused capabilities such as editing a document,
posting/reposting, opening a catalog and navigating pages. Business workflows use
`orion.model` directly rather than calling a second native command dispatcher.
The native backend still owns accounting math, persistence, import, document
editing hooks, report source adaptation and native dialogs. Those are not claimed
to have been rewritten in Lua. This preserves the existing editing-context and
posting tests while providing a shared architecture for further migration.

## References and choices

- [Lua 5.4 API](https://www.lua.org/manual/5.4/manual.html#4): VM/thread separation,
  registry references and userdata lifetime inform runtime ownership.
- [Apple WODisplayGroup and EOEditingContext](https://developer.apple.com/library/archive/documentation/LegacyTechnologies/WebObjects/WebObjects_3.1/WOBuilder/Script/Enterprise.html):
  declarative record operations, selection and editing contexts are the reference
  for codeless forms. Orion uses its own datasource protocol, not EOF classes.
- [Qt QQmlEngine](https://doc.qt.io/qt-6/qqmlengine.html) and
  [QQmlContext](https://doc.qt.io/qt-6/qqmlcontext.html): a shared engine with local
  component scopes informs the VM/window split. Qt also warns that implicit
  context properties hide dependencies from tools; Orion keeps sources/actions
  explicit in `.orion` and generates the native API contract.

## XML preprocessing: Etlua evaluation

The existing [lua-objc renderer](https://github.com/corepunch/lua-objc/blob/main/lua/ui/xml.lua)
preprocesses XML with Etlua expressions/statements, partials and inheritance.
Its [retained template boundary](https://github.com/corepunch/lua-objc/blob/main/lua/ui/template.lua)
then manages native view identity and reconciliation. That is more than string
substitution: structural changes also need ownership and update semantics.

For Ledger/Lodge, retain declarative forms without Etlua. Datasources already
supply record values and lists; DisplayGroups supply selection; editing contexts
own Save/Cancel; includes split large definitions into files. A preprocessing loop
would generate controls once, not automatically track subsequent database changes.
Re-rendering that XML would need to preserve selection, focus, unsaved edits and
native control identity. It would also put executable Lua ahead of static form
inspection, localization extraction and generated schema/API validation.

Reconsider an optional, explicitly marked template boundary only for a concrete
need to generate an unknown-at-authoring-time control structure, such as a
metadata-driven questionnaire. Prefer extending schema-driven forms for database
fields first. Any future Etlua boundary must define escaping, restricted execution,
repeatable output, tool support and view-state retention; do not execute templates
implicitly during schema loading. This decision does not change the existing
shared Lua template plugin used for printable documents.

## Verification

`test_script_runtime_test` covers controller package isolation, ownership checks,
codeless selection bindings and commit/rollback notifications. `test_lodge_test`
covers hotel rules, persistence, direct native-write validation, the shared app
host and computed values. Existing Ledger assistant/MDI tests exercise Lua command
routing against real six-country schemas and independent document contexts.
