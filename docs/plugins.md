---
layout: default
title: Lua Plugins
parent: Lua Add-ons
nav_order: 1
---

# Portable Lua plugins

Plugins package small, reusable behavior for any Orion application or add-on.
They run in Orion's existing Lua sandbox, use the same generated C bindings,
and ship as source with the application. No shared library, native Lua module,
platform-specific binary or second scripting runtime is needed per plugin.

A plugin is a Lua module; an **add-on** is a screen (`.orion` + Lua) that can
consume several plugins. Application rules stay in the application. For
example, the `intervals` plugin knows whether date intervals overlap; Lodge
knows which reservation states occupy a room. Ledger's penalties add-on and
Lodge both consume the same `calendar` and `money` plugins.

## Use a plugin

```lua
local Plugins = require("orion.plugins")
local calendar = Plugins.use("calendar", 1)
local intervals = Plugins.use("intervals", 1)

local tomorrow = calendar.add("2026-10-28", 1)
local nights = calendar.between("2026-10-28", "2026-10-31") -- 3
assert(not intervals.overlap("2026-10-28", "2026-10-31",
                             "2026-10-31", "2026-11-02"))
```

`Plugins.use(id, interface_version)` loads a plugin and its declared
transitive dependencies once per Lua state. Repeating it returns the same
exports. Different add-ons have different states and plugin instances.
The version is a positive integer interface version, **not** a SemVer range.
The loader checks the requested version even when a plugin is already loaded.

## Write a plugin

Put two files in `share/lua/orion/plugins/<id>/`:

```text
share/lua/orion/plugins/intervals/
  manifest.lua
  main.lua
```

IDs match `[a-z][a-z0-9_]*`. The installation's `make share` copies these into
`build/share/orion/lua/orion/plugins/`; install and desktop bundles preserve
that directory. Do not hard-code checkout paths in Lua. Hosts already using
`script_state_new()` / `script_page_open()` get this module search root.

```lua
-- manifest.lua: data only, with every plugin dependency declared.
return {
    id = "intervals",
    api = 1,             -- Orion's plugin manifest/factory protocol
    version = 1,         -- this plugin's exported interface
    dependencies = {calendar = 1},
}
```

```lua
-- main.lua: return a factory, which receives the declared plugin exports.
return function(dependencies)
    local calendar = dependencies.calendar
    local Intervals = {}
    function Intervals.valid(first, last)
        local ok, days = pcall(calendar.between, first, last)
        return ok and days > 0
    end
    return Intervals
end
```

Factories return a table. Keep them free of UI creation, database writes,
global registration and other side effects: loading should only construct the
plugin's API. Pass application models and callbacks into exported functions.
Core modules such as `orion`, `orion.db` and `orion.model` may be required
normally. Get other plugins through the injected dependency table, rather
than requiring their private files or calling `Plugins.use` inside a factory.

Dependencies are supported. They must form a directed acyclic graph. The
loader checks manifest identity, protocol and interface versions, reports
missing modules and cycles, and initializes dependencies before consumers
(in name order for independent dependencies). A failed factory is not cached,
and its visiting marker is cleared so a corrected load can be retried.
Successfully initialized dependencies remain available after a consumer fails.

Avoid adding dependencies just to share one value. An application can compose
two independent plugins by supplying callbacks. A plugin must never depend on
Ledger or Lodge. Do not split tightly coupled private helpers into separate
public plugins merely to make more files.

## Included plugins

| Plugin | Dependencies | Interface version 1 |
|---|---|---|
| `calendar` | None | `day(iso)`, `add(iso, integer_days)`, `between(first, last)` |
| `money` | None | `minor(number)`, `round(number)` |
| `intervals` | `calendar` | `valid(first, last)`, `overlap(a, b, c, d)` |
| `records` | None | `bind("db.table", validate?)` → record commands |
| `template` | None | `html(value)`, `render(text, fields)` |

Calendar dates use the proleptic Gregorian calendar, years 0001–9999, and
strict `YYYY-MM-DD` strings. Arithmetic is independent of timezone and DST.
Intervals are half-open: `[arrival, departure)`; departure and the next
arrival may fall on the same date.

Money uses a fixed two-decimal scale and rounds half away from zero.
`minor(12.34)` returns integer `1234`. Aggregate those integers and convert
back only for display/storage. It is not a general foreign-exchange or tax
engine and does not implement currencies with other decimal scales.

Templates substitute `{{field_name}}`, HTML-escaping every value, and reject
missing fields. They do not interpret Lua, render loops, support raw HTML
slots or provide safe substitution into JavaScript, CSS or URL contexts.
Native hosts own file output and printing; the plugin only returns text.

### Record commands

```lua
local records = Plugins.use("records", 1)
local guests = records.bind("hotel.guests", function(fields, previous)
    if not fields.name or not fields.name:find("%S") then
        return nil, require("orion").tr("Enter a name.")
    end
    return true
end)

local guest, message = guests:save(nil, {name = "Ada"}) -- create
local changed, reason = guests:save(guest.id, {name = "Ada Example"})
local ok, refusal = guests:delete(guest.id)
local rows = guests.model:all()
```

Validation receives the merged fields and previous row (`nil` on creation),
and returns `true` or `nil, message`. Existing rows are not changed on a
validation refusal. Defaults, field types, relationships, delete rules and
change scopes remain the datasource's responsibility. Use
`records.model:transaction()` plus `orion.assert()` for multi-record commands;
a returned refusal must be raised inside the transaction to trigger rollback.
Validation callbacks should be read-only. This module does not implement
permissions, database uniqueness constraints or cross-process locking.

## Forms and native hosts

Declare static UI and schema in `.orion` and compose the plugins in the page's
Lua script, as described in [Lua Add-ons](scripting.md). A plugin does not
need a form. If a feature needs its own screen, provide it as an add-on that
uses the plugin. The page owns rendering and receives typed input values;
plugins do not draw controls or manage window lifetime.

The host owns persistent database registration, save failures and output
paths. Native operations must use generated `.cgen` bindings; do not add
hand-written Lua bindings or expose arbitrary filesystem access. See
`apps/lodge/main.c`, `apps/lodge/store.cgen`, and `apps/lodge/share/` for a
complete host, schema, form and Lua controller.

## Portability and trust

Plugins have the existing add-on sandbox's capabilities: no `io`, `loadfile`,
bytecode/native module loading or arbitrary OS calls. All plugins in one page
share that page's Lua state and datasource access. Manifests are executable
trusted Lua, and dependency declarations are an architecture contract, not
a security boundary against malicious code. There is no remote plugin
marketplace, automatic download, permission broker or hot unload in this API.
The host packages reviewed plugin source with the app. Native UI, databases
and rendering stay in Orion; the same plugin source can run on every host
that supplies Orion's Lua 5.4 API. This does not by itself add mobile packaging
support to a new application.

## Test a plugin

`tests/fixtures/plugins/spec.lua` runs inside the actual Orion sandbox through
`tests/plugin_test.c`. It covers dependencies, cycles, version conflicts,
retry after failure, dates across DST/leap boundaries, money and HTML escaping.
`Plugins.new(resolver)` creates an isolated loader for tests. The resolver
receives `(id, "manifest"|"main")` and returns the corresponding module value.
Production normally uses `Plugins.use` and its bundled module resolver.

```sh
make share build/bin/test_plugin_test build/bin/test_lodge_test
build/bin/test_plugin_test
build/bin/test_lodge_test
```
