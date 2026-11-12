# Assistant workflow coverage

This matrix compares the local assistant's executable surface with recurring accountant and 1C developer work in the task research set. “Available” means an implemented path exists in this checkout; it does not mean coverage of arbitrary 1C configurations or production books.

## Accountant workflows

| Workflow | Current path | Coverage | Main gap / next scenario |
|---|---|---|---|
| Find counterparties, documents and items; filter posted state, document date, receivable debt or payable amount; sort; open a record | Typed Zork-style verbs, nouns, adjectives and follow-up refinements (`assistant_examples.lua`, dialogue suites) | Available for Ledger's own records; names must be quoted | ISO document-date comparisons are supported. Contracts and overdue status need explicit source semantics |
| Ask what needs attention, where a balance went negative, month close state, cash totals, payables, receivables, taxes or a reconciliation | Question topics backed by the Home checks, figures and movements (`assistant_palette_test.lua`, `ledger_assistant_test.c`) | Read-only answers; reconciliation requires one identifiable counterparty | The topic answers have fixed periods and fixed report scope. Reject unsupported qualifiers rather than dropping them |
| Import/update item, counterparty and rate catalogs from CSV | `import.items`, `import.counterparties`, `import.rates`, `prices.update`; preview produces a change plan | Automation runner only; not callable through the assistant grammar. Preview and explicit apply exist; keys are validated | Excel must be saved as CSV. No arbitrary external schema, multi-catalog transactions or import of posted documents |
| Detect duplicate catalog/document rows and missing required values | Assistant: `find duplicate customers/items` by country + Tax ID / SKU; automation: `duplicates.*`, `check.required` | Reports candidates/issues; does not merge or repair automatically | Human adjudication and duplicate-resolution UI are absent |
| Inspect item stock and counterparty settlements by configured account selections | `report.stock`, `report.settlements` | Automation runner only; read-only totals with explicit account selection and date bounds | No inferred account mapping, aging, contract detail or automatic reconciliation |
| Check a period before close | `check.month` | Automation runner only; reports unposted documents and missing exchange rates | Not a complete close checklist; it does not close or repair the period |
| Prepare a quarterly budget | `budget.q1` worksheet | Prototype for two reviewed demo charts; user-entered proposals remain separate from actuals | Hard-coded demo-chart mappings need to move to configuration-owned semantic roles before broad use; not cash-flow/tax forecasting |
| Post, delete, pay, settle, reconcile a bank statement or modify accounting entries | No general typed write command | Home-check suggestions and menu matches can invoke existing actions when selected; general bulk writes are unavailable | Any future write workflow needs a complete preview, validation, explicit apply, stale-data checks and recovery proof |

## 1C developer workflows

| Workflow family from task research | Current path | Coverage | Main gap / next scenario |
|---|---|---|---|
| Validate file columns, map rows, report import errors, preview catalog changes | Automation CSV/map/required/match/plan nodes and import recipes | Strongest current developer-adjacent area; plans require explicit apply | Realistic multi-sheet XLSX, references and linked entities are unsupported |
| Reconcile/aggregate register movements and produce a checkable report | Stock/settlement/month nodes and CSV formatting | Useful for bounded data checks; chosen account codes and report meanings are explicit | No general query designer, virtual-table/СКД authoring or invoice aging |
| Diagnose an existing BSL procedure/query error | No BSL parser, compiler, source index or runtime in assistant | Not covered | Build an isolated fixture workflow with source context, version/runtime assumptions, suggested patch and compile/test evidence |
| Change forms, event handlers, posting logic or extensions | No 1C metadata/extension editor or execution sandbox | Not covered | Requires versioned metadata model, extension-conflict checks, test infobase and before/after preview |
| Integrate HTTP/OData/SOAP, scheduled exchange or equipment | No connector designer or delivery runtime | Not covered | Start with contract tests for auth, timeout, 429, invalid payload, idempotency and retry; do not claim delivery automation from a generated snippet |
| Diagnose access, client/server context, lock or deployment failures | No 1C environment/session/rights diagnostics | Not covered | Needs environment inventory and logs with explicit permission boundaries |

## Interpreting “most tasks”

The task research describes candidate work patterns, not a measured share of any team's workload. The current assistant covers Ledger-local inquiries. The separate automation runner supports controlled catalog workflows; it is not yet exposed through the assistant grammar; it cannot replace general BSL development. Measure the “most tasks” claim against a consecutive, anonymized sample of real requests, counting setup, clarification, human review, correction and failure recovery. Track accepted first-pass results and human time separately, since many short catalog tasks can dominate task count while a few code changes dominate effort.

Before any write, the user must see the exact affected rows and before/after values. A preview is not evidence that an operation is safe to apply; validation and stale-snapshot checks remain required.

## Verification in this change

Three Luna agents inspected the parser, practical workflows, and native Ledger
behavior. The native core suite passed 36 tests covering posting/unposting,
balances, closing, previews, rollback and import. Automation, executor, add-on
and runtime-form suites also passed. Assistant tests exercise six country
configurations, with additional grammar, dialogue, identifier, topic, date and
budget regressions.

The visible German demo was checked in a disposable infobase: a creditor
threshold returned the same 1,428.00 as Home, the payable topic followed by
`open first` opened the named counterparty, a March date interval followed by
`only draft` returned AR-0002, and `taxes in 2025` refused the unsupported period.
These checks do not certify every country configuration or statutory workflow.

## Next architectural boundary

Keep surface words and inflections in the vocabulary, typed requests in the
parser, identity and missing-object questions in the engine, and accounting
behavior behind published Ledger capabilities. Duplicate checks are a descriptor
for each supported entity, not a collection of sentence handlers.

The next useful extension is a typed bridge to the existing automation recipe
registry: discover a recipe, fill its required slots through the same dialogue,
run read-only checks or prepare a change plan, display affected record identities
and before/after values, then explicitly apply that exact plan through the
existing transaction boundary. Do not make arbitrary generated Lua or BSL a
new verb. CSV mappings, duplicate resolution, print variants and custom reports
can share that protocol; a live 1C adapter and BSL tooling remain separate work.
