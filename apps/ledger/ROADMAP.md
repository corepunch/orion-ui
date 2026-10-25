# Ledger automation roadmap

## Available locally

- [x] Lua recipes and typed executable node graphs using one public Ledger API.
- [x] Searchable recipe/node metadata with tags and Russian task aliases.
- [x] Preview and transactional apply for catalog changes.
- [x] XML-generated API bindings, argument guards, editor annotations and docs.
- [x] XML-generated catalog metadata, node ports/parameters and reference.
- [x] Headless local runner and integration tests against real Ledger databases.

## Standard plugins form the application

- [x] Start the shipped Lua functionality from `share/stdplugs`; use the same
  ordered entry-script loader for standard and custom `plugins` directories.
- [x] Use one XML-generated contribution contract and registry for standard and
  custom nodes/recipes; reject duplicate IDs.
- [ ] Add package manifests with dependency/API compatibility checks, persisted
  enable/configuration state, migrations and plugin-manager UI (Lite XL and 1C
  extensions are references; standard packages remain vendor-owned).
- [ ] Move document workflows, reports, posting, payroll and closing from native
  implementations to first-party Lua standard plugins, with parity tests.

## Script-first application architecture

See the [source audit and 3ds Max-inspired migration plan](docs/script-first-architecture.md).

- [ ] Reuse Orion's runtime schemas, script pages and model layer; publish Ledger
  operations to that host with instance-scoped contexts and XML contracts.
- [ ] Add a shared stable action registry for UI, CLI, Lua and nodes, including
  enable/checked state, typed parameters and later semantic macro recording.
- [ ] Split recalculation/posting/after-post hooks and make native posting atomic
  with failure propagation before migrating built-in accounting policies.
- [ ] Move first-party workflows, policies and reports to Lua in tested stages;
  keep schemas/forms/contributions declarative and native services reusable.

## Next local increments

- [ ] Catalog UI with task search, parameter forms, results and change previews.
- [ ] Visual graph editor consuming the existing generated node metadata; preserve
  the same Lua graph executor and operation implementations.
- [ ] Named/persisted graphs and plans, run history and supported cancellation.
- [ ] Additional import mappings, optional XLSX reader and banking adapters.
- [ ] Dedicated document/draft APIs with validation, posting dependencies and
  explicit handling of closed periods before allowing document automation.
- [ ] Confirmed duplicate merging with a reference-transfer plan and tests.
- [ ] Extend XML contracts with structured result types, localization catalogs,
  richer parameter constraints and generated UI forms; keep XML authoritative.

## 1C compatibility — later, not part of the local implementation

- [ ] Choose one initial configuration and tested version range (for example
  Accounting 3.0); define a compatibility matrix and synthetic fixtures.
- [ ] Build a configuration-specific adapter via OData/HTTP or a local extension.
- [ ] Map semantic objects and operations, not just field names: references,
  register semantics, posting rules, transactions and matching identifiers.
- [ ] Reuse a declared portable subset of scenario operations for both Ledger and
  1C. Explicitly reject unsupported nodes and platform-specific Lua hooks.
- [ ] Explore generating BSL for that subset. Arbitrary Lua-to-BSL conversion is
  not a compatibility promise.
- [ ] Treat generation/compilation of external processors (.epf) and extensions
  (.cfe) as separate packaging work with supported 1C tooling and validation.

## Online catalog and scenario writer — later

- [ ] Publish versioned metadata from the same XML contracts and expose search
  by business task, tags, compatibility and required capabilities.
- [ ] Add authoring by forms, graph or text, with preview and executable examples.
- [ ] Keep execution local by default; define a separate explicit model for any
  remote execution and company data transfer.
- [ ] Before accepting third-party code, implement isolated execution, capability
  restrictions, CPU/memory limits, package provenance and update/version policy.
- [ ] Add installation/review workflows, compatibility checks and run diagnostics.

No 1C conversion, online hosting, external publishing or remote execution is
implemented by the current local recipes/nodes work.
