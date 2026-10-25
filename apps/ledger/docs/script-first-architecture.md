# A script-first Ledger, using 3ds Max as the architectural reference

## Conclusion and scope

Yes: Ledger can ship its application workflows and accounting policies as Lua
plus `.orion` resources, with a small native host and reusable native services.
The target is that a shipped feature and an add-on use the same published API,
commands, models and form loader. It is not necessary to translate the rendering,
storage or transaction implementation into Lua to achieve that goal.

This is a source audit and migration proposal, not a claim that the migration is
implemented by the automation PR. This audit includes the upstream runtime-schema
and Lua add-on work present in main at `58c5254`.

## Standard plugins are the product

The primary reference is 3ds Max's plugin composition: a small host loads the
standard functionality supplied with the product through the same extension
mechanism used for custom plugins. Autodesk documents plugin search paths,
including `stdplugs`, in its [Plug-in Directory Search Mechanism](https://help.autodesk.com/cloudhelp/2023/ENU/Max-Developer-Help/writing_plug-ins/advanced_plug-in_topics/plug-in_directory_search_mechani.html).
This does not mean that all Max plugins are scripts: its documented search
mechanism loads native DLL plugins as well.

Ledger's desired application layer should be predominantly Lua. The first step
in this PR is concrete: shipped automation is in `share/stdplugs`, and both
standard entry scripts and custom `<infobase>/plugins/*.lua` use the same ordered
loader. Their nodes/recipes use the same registry, XML-generated implementation
maps and generated public Lua argument wrappers. Duplicate contribution IDs
are rejected. Existing country-specific `configuration/plugins` remain a
compatibility layer for accounting hooks.

The rest of the built-in accounting/UI is still native in this PR. Moving files
into `stdplugs` alone is not enough: shipped accounting modules need to become
ordinary clients of published services. The migration map below identifies that
work, rather than claiming an empty host already exists.

[Lite XL's plugin documentation](https://lite-xl.com/user-guide/managing-plugins/)
(the successor to [rxi/lite](https://github.com/rxi/lite)) is a closer reference
for implementing this in Lua: bundled plugins live under the application data
folder, and user plugins under user configuration. Its
[configuration mechanism](https://lite-xl.com/user-guide/configuration/) permits
per-plugin options and disabling plugins before startup. Ledger should adopt
separate distribution/user ownership, explicit configuration, compatibility
checks and deterministic loading. Avoid copying arbitrary monkey-patching as
the primary accounting extension contract.

[1C configuration extensions](https://v8.1c.ru/platforma/rasshireniya/)
provide another useful reference: assess applicability before enabling an
extension, keep the vendor configuration separate, and make extension order
explicit. This informs local package management; it does not add a 1C adapter
to the current scope.

The eventual plugin manifest should declare a permanent package ID, package/API
versions, dependencies, contributions, configuration, capabilities and migrations.
Use `stdplugs` for vendor-owned packages and `plugins` for custom packages; do not
edit vendor files to customize behavior. Never silently replace a standard
contribution on an ID collision. Explicit extensions/overrides, disable state,
dependency resolution and a plugin manager are subsequent increments, not
implemented features of the initial ordered loader.

## Supporting published APIs, actions and macro scripts

Autodesk's [Function Publishing](https://help.autodesk.com/cloudhelp/2024/ENU/Max-Developer-Help/3ds_max_sdk_features/function_publishing.html)
exposes native operations through discoverable interfaces callable from
MAXScript. For Ledger, XML should publish the operation's types and semantics;
UI commands, Lua and nodes should call that same operation. Native services can
remain native without making the application closed to scripting.

[MacroScripts](https://help.autodesk.com/cloudhelp/2024/ENU/MAXScript-Help/files/MAXScript-Tools-and-Interaction/Creating-MAXScript-Tools/GUID-6E21C768-7256-4500-AB1F-B144F492F055.html)
associate named scripts and categories with actions such as menu items, toolbar
buttons and shortcuts. Ledger needs stable action IDs, contextual enable/checked
state and one invocation path, rather than one C switch for built-ins and a
separate runner for user scripts.

[Scripted plug-in parameter blocks and rollouts](https://help.autodesk.com/cloudhelp/2020/ENU/3DSMax-MAXScript/files/GUID-461915FA-31A2-49CE-84AF-2544B782ACA3.htm)
provide persistent parameters and a UI associated with those parameters. The
Ledger equivalent is typed schema/parameter declarations and `.orion` forms,
with Lua supplying actions. Persistent IDs and version migration matter as much
as the UI: renaming a saved parameter or command is a compatibility change.

The [Macro Recorder](https://help.autodesk.com/cloudhelp/2022/ENU/3DSMax-Basics/files/GUID-7F0698EB-236E-48EF-96B3-4DF011DF86B0.htm)
records many user operations as script calls. A later Ledger recorder should
record successful semantic action invocations with explicit parameters. Mouse
coordinates, control IDs and incidental redraws are unsuitable replay data.
Autodesk documents incomplete recorder coverage; this is an architectural
reference, not a claim that every part of 3ds Max is scripted.

## Existing foundations and actual gaps

- `ledger.orion` already declares schemas, forms, menus, bindings, defaults and
  delete relationships. Country directories hold accounts, rates, scales,
  document definitions, closing rules and HTML/CSV templates.
- `ledger_script.c` already dispatches custom posting/tax/closing rules and
  `post` subscriptions. The automation PR publishes its Lua bindings and docs
  from XML, plus local recipes and typed node graphs.
- Upstream `orion/commctl/form_loader.*` loads forms and now creates declared
  database schemas at runtime. `ledger_runtime_forms_test.c` checks parity.
- Upstream `orion/script/` supplies a page host: `.orion` plus Lua `init`, `data`
  and action methods. `share/lua/orion/model.lua` supplies typed record/query
  wrappers over datasource messages. Use these rather than creating another UI
  host, layout system or record model inside Ledger.
- Runtime forms still reject some constructs requiring compiled command IDs,
  including toolbars, activity pages and context menus. A general action
  registry is needed before the entire Ledger shell can be a runtime add-on.
- Runtime schemas do not make existing C accounting code schema-independent:
  `posting.c`, `ledger_ib.c` and generated `db_*` structs still know specific
  tables and fields. Removing that assumption is part of the migration.
- There are currently two Lua environments: Ledger's shared trusted hook state
  and Orion's isolated page states. The local runner and initial standard plugin use the former. The
  recipes are not yet a sandboxed `script_page` module: their bootstrap uses
  `debug`/`loadfile`, and their host publishes a global `ledger` table. Module
  loading and context ownership must be adapted before claiming a common host.
- Orion's generic model CRUD is not the automation catalog's restricted
  `ledger.insert/update` API. Cross-cutting Ledger invariants must live at the
  authoritative datasource/command boundary, regardless of which Lua facade a
  caller uses. Model-local constraints alone do not govern every caller.

## Migration map from current files

The initial source snapshot has 906 lines in `posting.c`, 378 in `controller.c`,
65 in `ledger_ib.c`, 265 in `print.c` and 504 in `import_1c.c`. These are file
sizes, not estimates of migration percentage or performance.

| Current responsibility | Proposed Lua / `.orion` ownership | Native responsibility and missing API |
|---|---|---|
| `controller.c`: `ledger_command`, catalog commands, document form selection | Named Lua commands, `.orion` action contributions and form lookup by name | Reusable action registry, selection/context access and form/page hosting; remove compiled-ID dependency incrementally |
| `controller.c`: `new_document`, `after_save`, `payroll_hook`, `month_end` | Lua workflow/default/parameter logic; `.orion` parameter forms | Draft editing and explicit commit/post commands; do not invoke ledger mutations as raw widget messages |
| `ledger_ib.c`: insert/update/delete recalculation dispatch | XML declares lifecycle handlers; first-party Lua implements numbering/recalculation policies | Datasource owns atomic scopes, dispatch order, recursion guards, errors and invalidation; every C/Lua/form writer follows it |
| `posting.c`: `ledger_number_document`, `ledger_fill_payroll` | Lua numbering and payroll-fill policies selected by configuration | Typed draft/line create/update/remove operations, numbering uniqueness check, transaction and date/query services |
| `posting.c`: `recalc_goods`, `recalc_payroll`, `recalc_assets` | Lua document calculators | Typed writable calculation results, stable money/date semantics and lifecycle invocation; existing catalog-only write API is insufficient |
| `posting.c`: `post_goods`, `post_cost_of_sales`, `post_payroll`, `post_assets` | Shipped Lua posting modules; XML selects the implementation per document type | Validated entry builder, account/analytics lookup, turnover snapshot, transactionally managed posting context |
| `posting.c`: `line_taxes`, `scale_tax`, `tax_in_force`, `ytd_pay` | Lua tax/base/allowance/cap policy; effective dates, rates and scale data remain XML | Efficient historical aggregate queries, explicit rounding service and reproducible ordering; preserve all four country/year fixtures |
| `posting.c`: depreciation, transfers, revaluation, closing sequence | Lua closing steps and sequencing; XML declares enabled steps and their parameters | Common atomic document/posting context, balance/query primitives and propagation of failed steps |
| `posting.c`: `build_trial_balance`, `build_card`, `build_analysis`, VAT/cash/payroll/asset reports | Lua report definitions/calculation; `.orion` declares result tables and forms | Efficient scan/group/query APIs, typed publication/replacement of derived rows, output isolation and period context |
| `print.c`: template selection and output workflow | Lua chooses forms/data; existing HTML/CSV files remain templates | Reusable escaping/template renderer, locale formatting and explicit file-output service; no reason to duplicate the rendering engine in Lua |
| `import_1c.c`: field mappings, matching and object-specific conversion | Potential Lua adapters and declarative mappings in a later import phase | XML parser, streaming/file services, digest, atomic import context; 1C adapter work remains separate roadmap scope |
| `main.c`: configuration selection, organization initialization, page composition | Lua startup/application composition, `.orion` contributions and package manifest | Minimal executable lifecycle, resource resolution, library registration, persistence and window-system lifetime |

## Proposed shared contract

XML should describe functions, structured types, errors, effects/capabilities,
parameters/defaults, examples and version/deprecation metadata. Generate native
bindings/guards, Lua annotations, discoverable descriptors and reference pages
from it. The current generator covers signatures, guards and reference; richer
structured contracts and action generation are future work.

Use `.orion` for forms, schemas, menu/toolbar contributions and references to
stable actions. Use Lua for calculations and workflows. Avoid inventing an XML
expression language alongside Lua. The existing country `configuration.xml`
files remain configuration data; extension packages can supply additional
`.orion` schemas/forms without duplicating them in C.

A future package could contain:

```text
stdplugs/ledger-core/
  package.xml              version, dependencies and contributions
  documents.orion          schemas, document/parameter forms
  reports.orion            result schemas and report forms
  actions.orion            references to stable commands
  lua/documents.lua        first-party workflow commands
  lua/posting.lua          first-party accounting policies
  lua/reports.lua          first-party reports
```

This directory layout is illustrative; a package manifest, command registry and
these action contributions are not implemented in the current PR.

All entry points should converge on:

```text
menu / toolbar / CLI / Lua / node
                  ↓
        named action + typed parameters
                  ↓
       shared validation and transaction
                  ↓
          Lua workflow / policy
                  ↓
       published native data services
```

Selection is an input resolved by the host into explicit record references;
headless calls supply references directly. Add-on pages continue to use Orion's
existing `data()` request model rather than building a second observer/binding
framework. Hooks should receive an instance-scoped context, not reach into a
process-global active document.

## Prerequisites before moving built-in accounting

1. **Separate recalculate, post and after-post.** Today assigning
   `doc_types.script` also makes `ledger_recalc_document()` use `sum_entries`.
   Simply setting a script on every existing type would change totals before
   posting. Declare independent lifecycle hooks and their typed outputs.
2. **Make posting atomic and failure-aware.** Today `ledger_post()` deletes old
   entries before a custom hook runs; a failure can return before restoring
   them. `post_closing()` does not propagate the boolean result of a scripted
   closing step, and `post` listener errors are logged separately. Define which
   events are mandatory and roll back failures before exposing general commands.
   The new automation runner's outer transaction does not change native UI
   posting semantics.
3. **Unify validation across writers.** XML types provide shape, but business
   validity also needs account/reference checks, finite monetary values and
   lifecycle rules at an authoritative boundary. Per-page constraints must not
   become the only enforcement point.
4. **Publish operations with ownership.** Add explicit draft/line APIs, immutable
   read snapshots, typed results, query/aggregate services, temporary report
   tables and print/form services. Register the Ledger module in page states
   through the existing Orion host; eliminate ambient globals gradually.
5. **Preserve arithmetic and ordering.** Migration must keep round-half behavior,
   progressive/YTD calculations, same-day posting order and the existing special
   treatment of manual-operation entries. Do not silently change number types
   in the same refactor.
6. **Measure queries before choosing their language.** Current fetch calls copy
   complete result arrays into Lua; reports and YTD calculations need batched
   queries or stable snapshots. Compare the committed large synthetic samples
   as well as small demos before accepting performance changes.
7. **Persist identifiers and migrate parameters.** Package/action/schema IDs and
   parameter versions must survive upgrades. Define reload behavior for open
   forms and rule changes; do not hot-reload accounting during an active post.

## Recommended sequence and acceptance gates

1. Publish commands and page/form/query services through the XML contract;
   integrate with Orion's host. Demonstrate one built-in command and one add-on
   using the same registration, enable state, transaction and error handling.
2. Move numbering/defaults/payroll-fill and a simple read-only report first.
   Keep UI/headless equivalence tests and runtime/compiled form parity tests.
3. Split lifecycle hooks and fix atomic posting, then move one goods document
   to Lua. Compare every entry's account, analytics, currency, quantity and
   amount, plus totals and failure rollback, against the native baseline.
4. Move payroll and closing policies in small increments. Compare every
   ru/de 2003/2026 fixture and repeated/reordered posting, including negative and
   boundary values. Retain native primitives only where they provide shared
   integrity or measured performance benefits.
5. Move report orchestration and the remaining Ledger shell, then add a semantic
   macro recorder and user-command composition. Record successful commands only;
   replay must pass the same validation and cannot bypass document lifecycle.

Do not keep C and Lua implementations as permanent production fallbacks: use
baseline comparisons during migration, then remove the replaced branch. The
observable end state is that a normal accounting feature can be changed,
documented, loaded and tested without recompiling Ledger's native host.
