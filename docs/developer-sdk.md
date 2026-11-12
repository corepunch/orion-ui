# Developing business applications on Orion

Orion is the common application runtime. Lodge is a declarative hotel application;
Ledger combines the same UI runtime with a native accounting engine. Both consume
portable Lua plugins. You can develop a folder application without writing or
recompiling C. Extending the native engine still requires its C toolchain.

## Start outside this checkout

Prerequisites: Python 3.9+, the built Orion runtime, and Lua/luac matching that
runtime. The SDK accepts Lua 5.4 and 5.5, as the current native build does. Prefer
5.4 for releases targeting 5.4; a 5.5 syntax check does not prove 5.4 compatibility.
Set `ORION_LUA` and `ORION_LUAC` if your PATH points at a different version.
No Python packages or downloaded dependencies are needed by the SDK itself.

From the checkout:

```sh
python3 tools/orion_sdk.py new-app /tmp/my_reception --name my_reception
python3 tools/orion_sdk.py check /tmp/my_reception
mkdir /tmp/my_reception_data
build/bin/orion-run /tmp/my_reception --data /tmp/my_reception_data
python3 tools/orion_sdk.py pack /tmp/my_reception /tmp/my_reception.zip
```

The starter is a persistent record list and editing form with native Save/Cancel
editing contexts. Static UI, schema and bindings live in `application.orion`.
Use [Lodge](../apps/lodge/README.md) as the complete reference for domain validation,
state transitions and computed fields. Use [declarative applications](declarative-applications.md)
for runtime ownership and [database forms](database-forms.md) for schema rules.
The native runtime test opens the same starter template, saves a record and
reopens it from disk. The starter has no hotel or accounting business rules.

## A plugin usable by either application

```sh
python3 tools/orion_sdk.py new-plugin /tmp/stay_dates --name stay_dates
python3 tools/orion_sdk.py check /tmp/stay_dates
python3 tools/orion_sdk.py pack /tmp/stay_dates /tmp/stay_dates.zip
```

The generated plugin exports `next_day(date)` and declares its dependency on
calendar interface 1. In a reviewed host distribution, place its source folder at
`share/lua/orion/plugins/stay_dates/` (the checkout's shared source tree), run
`make share`, and consume it from either app:

```lua
local plugins = require("orion.plugins").load({stay_dates = 1, money = 1})
local next_date = plugins.stay_dates.next_day("2026-11-12")
```

This is host-packaged source installation. ZIP creation does not install a plugin,
change an existing app, or grant new capabilities. A distributed runtime uses
`share/orion/lua/orion/plugins/`. Do not overwrite installed files while the host
is running. Independent Ledger add-ons remain isolated VMs; trusted controllers
within an application share their runtime. Ledger's accounting rule scripts and
`stdplugs` are a separate API; they are not portable plugin factories.

## Compatibility contract

- `api = 1` is the plugin manifest/factory protocol. `version = 1` is the exact
  exported interface version, not a release number or SemVer range.
- Every plugin dependency declares its exact interface. Breaking arguments,
  results or behavior requires a new interface version. Compatible fixes retain
  the interface version; record their source revision and release notes separately.
- `Plugins.inspect({id = version})` returns copied manifests in dependency order
  without loading factory modules. `Plugins.load(requirements)` preflights the
  entire requested graph and factory types before calling any factory.
- Cycles, conflicting versions, missing dependencies and unsupported protocol
  versions reject the request. A runtime keeps one interface per plugin ID.
- Factories must be side-effect free and consume injected dependencies. Recursive
  loading during factory initialization is rejected. After a factory throws,
  successfully initialized dependencies remain cached; the failed factory can be
  retried. This is not a transaction for arbitrary factory side effects.
- Inspection executes trusted manifests. SDK inspection uses an empty Lua
  environment and a timeout, but neither it nor a checksum establishes trust.
  Do not review or execute untrusted code as if it were inert package metadata.
- New-record validation through `records.save` receives typed schema defaults
  from `Model:defaults()` / `orion.db.defaults`; keys and computed fields are omitted.
- Native `.cgen` contracts generate bindings and editor declarations; their API
  versions are not covered by portable plugin interface numbers. Pin the tested
  Orion commit/release and the host app revision in your release documentation.

See [plugin authoring](plugins.md) for lifecycle, sandbox limits and dependencies.
Avoid private C structures, internal Lua files and undocumented fields. Application
validation belongs in the model/datasource so native form edits cannot bypass it.

## What the checker and packer guarantee

`check` validates Lua syntax, plugin manifest dependency graphs against this
checkout's bundled plugins, and application XML includes, initial form and declared
model/controller files. When a plugin has `package.json`, it also validates the
package metadata and its agreement with the manifest, including runtime API,
interface and dependencies; `settings.lua` is reserved. It rejects include
escapes/cycles and package symlinks. A standalone plugin without `package.json`
can still be checked as source, but a distributable plugin archive requires it.
It is not the complete Orion schema compiler, an interpreter of dynamic `require`,
or a proof of accounting correctness. For external plugin dependencies, pass `--plugin-root /path/to/store/snapshots/GENERATION`
before `check` or `pack`; bundled manifests remain authoritative for bundled IDs.

`pack` checks a clean source directory, accepts only documented source/resource
extensions, caps individual files at 16 MiB and the package at 64 MiB, and writes
an archive outside the source directory. It refuses an existing output. Sorted
entries, fixed timestamps and modes make archives repeatable on the same toolchain.
The limits include the generated inventory; at most 4095 source files are allowed.
Names must not collide case-insensitively or contain colons or backslashes, and
`SHA256SUMS.json` is reserved regardless of case. These restrictions make plugin
archives compatible with the package manager's portable path checks.
`SHA256SUMS.json` records each source file's hash. Hashes detect accidental changes;
they are not signatures. Keep the source directory unchanged while packaging.
Never point the packer at a company data directory. Add license, tested versions,
release notes and domain tests before distributing a package.

## Verification before a release

```sh
python3 -m unittest discover -s tools/sdk -p 'test_*.py' -v
make lua-check
make share build/bin/test_plugin_test build/bin/test_script_runtime_test build/bin/test_lodge_test build/bin/test_ledger_test
build/bin/test_plugin_test
build/bin/test_script_runtime_test
build/bin/test_lodge_test
build/bin/test_ledger_test
```

The SDK suite runs in Linux CI, alongside native tests. Plugin tests cover graph
conflicts before initialization, cycles, factory failures and retry, cache identity
and inspection isolation. App tests check real databases and the native runtime.
Add your own tests for invalid input, cancellation, write failure, save/reopen and
updates from the previous supported data version. Use disposable data, with expected
results independently derived from the business rules.

## Production boundaries

This SDK makes the existing runtime accessible and checks source packages. The local package manager and schema XML migration/backup mechanisms described
below are now available, including optional local publisher signature checks.
A publisher identity service, extension permission broker and general schema
rollback remain unfinished. The shared server has fixed roles; general-purpose
multi-user authorization and hostile native-code isolation remain unfinished. See [platform readiness](business-platform-readiness.md) for the
remaining acceptance criteria shared by Ledger and Lodge. Keep those capabilities
separate from a claim of statutory accounting completeness.

## Install, configure and roll back reviewed plugins

The source scaffold now includes inert `package.json`: format, publisher, release,
plugin interface, dependencies and required runtime API. Publisher is a declared
identity; local administrators can pin a signing key to that identity. Installed `manifest.lua` is generated from this
metadata after verifying agreement with the development manifest. Installation
evaluates only the manifest in an empty Lua environment under an instruction
budget, and syntax-checks Lua files; it does not execute plugin factories.

```sh
python3 tools/orion_packages.py --store /tmp/my_plugins check-install /tmp/stay_dates.zip
python3 tools/orion_packages.py --store /tmp/my_plugins install /tmp/stay_dates.zip
python3 tools/orion_packages.py --store /tmp/my_plugins list
python3 tools/orion_packages.py --store /tmp/my_plugins verify
python3 tools/orion_packages.py --store /tmp/my_plugins diagnose > /tmp/plugin-diagnostics.json
python3 tools/orion_packages.py --store /tmp/my_plugins configure stay_dates /tmp/settings.json
python3 tools/orion_packages.py --store /tmp/my_plugins run build/bin/lodge --data /tmp/my_hotel
python3 tools/orion_packages.py --store /tmp/my_plugins disable stay_dates
python3 tools/orion_packages.py --store /tmp/my_plugins enable stay_dates 0.1.0
python3 tools/orion_packages.py --store /tmp/my_plugins rollback
```

The same `run` command accepts `build/bin/ledger`. Applications/add-ons explicitly
consume plugins through `Plugins.load` or `Plugins.use`; installation does not
implicitly execute them or add a menu item. Load package settings with
`require("orion.plugins.stay_dates.settings")`. Settings survive disable/re-enable;
rollback restores the previous active releases and package settings, never company data.

Release objects and per-launch snapshots remain separate from the active registry.
The manager serializes writers, checks hashes, paths, publisher consistency and
dependency compatibility, then publishes the selected generation by atomic file
replacement. Dependency validation occurs before writing a new release object;
a rejected dependency leaves both the registry and object store unchanged.
`check-install` validates the candidate, signature policy, dependencies and other
selected release objects without publishing it. It takes the manager lock and may
create the store and lock file. The store or archive can change after this check;
installation repeats the checks and is the authoritative decision. Bundled
manifests are evaluated in a restricted Lua environment and checked for matching
identity and interface instead of extracting a version from their text; a missing
or invalid manifest prevents activation or launch. Failed activation leaves the
previous registry available. If the final directory sync fails, publication has
an uncertain durable outcome: inspect `list` and run `verify` before retrying.
`verify` checks the SDK checkout's bundled interfaces, package objects, signatures
and selected snapshot without launching the application; `run` additionally checks
against the selected executable's runtime. A process killed during publication may leave an unreferenced snapshot
or temporary file; retain the active and previous generations and inspect the
store before manual cleanup. POSIX tests kill a separate process around snapshot
sync, registry write and replacement, then verify the active snapshot. They do not
simulate power loss. Running processes retain a fixed `ORION_PLUGIN_ROOT`; restart
to use a new generation.
External search is restricted to the `orion.plugins.*` namespace, after bundled
modules and before app-local plugin files. External packages cannot replace the
runtime's core modules. `run` checks bundled dependency versions beside the actual
executable; installation checks the SDK checkout's contract.

### Signed releases

Optional detached Ed25519 signatures require OpenSSL 3 or later; no Python
cryptography package is installed. The signature covers a domain-separated
SHA-256 digest of the canonical archive contents, including `package.json`.
Keep the publisher's private key outside the package and back it up securely.
For example, generate and distribute an Ed25519 public key through a separately
verified channel, then compare its printed SHA-256 fingerprint with the value
provided by the publisher:

```sh
openssl genpkey -algorithm ED25519 -out /secure/publisher-private.pem
openssl pkey -in /secure/publisher-private.pem -pubout -out /tmp/publisher-public.pem
python3 tools/orion_packages.py sign /tmp/stay_dates.zip /secure/publisher-private.pem /tmp/stay_dates.sig
python3 tools/orion_packages.py --store /tmp/my_plugins trust local /tmp/publisher-public.pem
python3 tools/orion_packages.py --store /tmp/my_plugins policy require
python3 tools/orion_packages.py --store /tmp/my_plugins install /tmp/stay_dates.zip --signature /tmp/stay_dates.sig
```

The `trust` argument must match the `publisher` field. A store defaults to
`policy optional`; a signed release is always checked against its pinned key,
and `policy require` rejects unsigned releases. Enabling that policy fails if
an active release is unsigned or has an invalid signature. Install, activation,
rollback and launch verify installed object contents and selected signatures;
launch also checks the generated snapshot, including settings. Key replacement
is deliberate: disable releases signed by the key, run `untrust PUBLISHER`,
pin the replacement, then install releases signed by it. The administrator must
secure the store and verify fingerprints out of band: a pinned key proves that
release bytes were signed by the holder of that key, not who that holder is.
No guarantee is made against another process with permission to modify the store
after launch verification, or against malicious code in an authorized plugin.

`diagnose` exports package identities, versions and hashes without configuration
values, company records or local paths. It describes package activation, not a
complete runtime crash report.

Old releases/snapshots are retained for rollback and running processes; this first
manager has no garbage collector. Protect the store with filesystem permissions.
It is reviewed local code, not a remote marketplace or a hostile-code permission
boundary. Linux/macOS file and directory synchronization is used; Windows directory
sync is not claimed. A sync failure after registry replacement is an uncertain
commit: inspect `list` before retrying. The activation fault test covers failure
before registry replacement, not every possible power-loss point.

## Runtime and data initialization contracts

New applications declare `<application runtime-api="1" .../>`. Unsupported API
requirements are rejected before databases are opened; omission retains legacy API
1 behavior. This is the shared application runtime contract, independent of each
plugin's exported interface version. Native Ledger extensions still require the
host's generated API and the tested application revision.

For `db_schema_xml` models, `initialize(database_name)` runs after model attachment
and before forms open. Return `true`, or `nil, message` to reject initialization.
Changes, including audit rows, remain in one database change scope; the host saves
them and commits only after success. A synchronization failure after replacing the
primary file has an uncertain on-disk outcome; inspect/recover before retrying. Failure discards the pending opened database
rather than saving it during teardown. Schema XML has an infallible valid-scope
commit; arbitrary datasource subclasses are not accepted for this initializer.
This is per-database atomicity, not a distributed transaction across several stores.

`validate_delete(table, previous)` protects native deletion as well as Lua deletion.
`after_write(table, operation, current, previous)` runs transactionally after an
insert/update/delete and must return `true` or a refusal. `current` is nil for delete;
`previous` is a copy of the original record. The hook can insert dependent/audit
records. Nested writes are validated but do not recursively invoke `after_write`;
put the complete audit policy in the outer hook. Mutating the originating written
record from a hook rejects and rolls back the operation. Validation and computation
cannot write; hooks cannot save or control the enclosing transaction.

See [persistence](persistence.md) for declarative schema versions, additive migrations,
locks, backups and recovery. Lodge supports explicit `--restore-backup --data DIR`
through the common launcher; Ledger supports the same switch in its native launcher.
Keep an independent backup before an upgrade. Recovery replaces the primary with a
previous committed snapshot and can therefore lose later operations; it is explicit,
never a silent fallback when opening a damaged database.

## Compatibility and deprecation policy

The machine-readable [current matrix](business-platform-api.json) is checked by
`make business-server-check` against the runtime constant, generated Ledger API,
plugin loader and package validator, and the two persistent schemas. Runtime API,
Ledger API, service protocol, plugin loader API, package format, each plugin's
exported interface and storage schema versions are independent numbers. The matrix
records the loader API and package format, not the interface version of every plugin.

- Published functions and argument meanings of `runtime-api="1"`, Ledger Lua
  API 1 and `/v1` remain supported within those interfaces. Additions must preserve
  existing valid calls and defaults. Rejecting an out-of-range value that previously
  wrapped or corrupted data is a bug fix, not a supported old behavior.
- Removing/renaming a public function or changing a valid argument's meaning
  requires a new major interface. First document the replacement and keep the old
  interface through one supported release cycle; a release cannot silently claim
  interface 1 while dropping its supported contract.
- Plugin release `X.Y.Z` identifies release bytes. Its integer exported interface
  is the compatibility contract; a semantic version label alone never overrides
  dependency/interface validation. A package cannot replace bundled interfaces.
- Persistent schema versions only move forward. An executable that supports an
  older schema must refuse newer data. Rolling back plugin code does not roll back
  company data. Restore a compatible backup explicitly when a schema downgrade is
  needed; preserve the current data separately first.
- Only documented APIs are covered. Internal `Hotel` Lua tables, window structure
  layouts, generated C record layouts and arbitrary native pointers are not stable
  binary extension interfaces. Native modules must be rebuilt and regression-tested
  with their host revision. Trusted Lua extensions still share the host's authority;
  signatures identify approved bytes, not a sandbox.

Shared-server integrations use the [data-only business protocol](business-service.md).
It reuses domain validation and avoids vendor-file changes; clients create, post,
reverse or book through named operations. The command-line client is an executable
example. Initial Ledger and Lodge desktop workspaces also bind to this protocol
when launched with `--remote`. They use a bounded, cancellable RPC transport and
transient projections; the exact pending request is stored before transmission
and can be retried after a restart with the same server, company and actor.
The bearer token is supplied by the trusted launcher, not as a command argument.
The desktop transport currently supports macOS and Linux; Windows, iOS, Android
and WebGL fail closed. These initial workspaces still need broader workflow,
accessibility and independent pilot validation. See [server operations](server-operations.md)
for the outbox, backup and recovery boundaries.
