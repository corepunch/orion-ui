# Ledger assistant TODO

Priorities from the [workflow audit](docs/assistant-workflow-coverage.md).
Keep vocabulary and inflections in XML, typed requests in the parser, object
identity and clarification in the dialogue engine, and accounting operations
behind published Ledger capabilities.

## Next: connect the existing automation recipes

- [ ] Expose recipe descriptors and required parameter types to the assistant.
  Reuse the automation registry rather than maintaining a second command list.
- [ ] Resolve recipe parameters through noun phrases, modifiers and missing-slot
  questions. Preserve the exact entity, period and filters through clarification.
- [ ] Connect read-only stock, settlement, required-field and month checks.
  Compare assistant results with the same recipes run headlessly.
- [ ] Connect CSV catalog imports and price updates through preview/apply.
  Show affected record IDs, matching keys, errors and before/after values.
- [ ] Apply only the reviewed plan after an explicit command. Reject stale
  inputs; test rollback, repeated apply, cancellation and partial failures.

## Accounting semantics and reporting

- [ ] Move the budget prototype's country-specific account mappings into
  configuration-owned semantic roles. Reject missing or overlapping mappings.
- [ ] Add explicit period/account parameters to topic answers and reconciliation.
  Never silently discard qualifiers or substitute the current report period.
- [ ] Add invoice aging only after defining due dates, payment allocation,
  partial settlements and credit-note handling.
- [ ] Add saved report and print variants through the existing forms/templates.
  Verify source-to-total reconciliation and rendered output.
- [ ] Add duplicate-resolution plans with explicit survivor selection, reference
  migration and transactional verification. Identifier matches remain candidates.

## Dialogue and discoverability

- [ ] Generate supported-command examples and help from capability descriptors.
- [ ] Extend date ranges and missing-date clarification in the typed grammar,
  with equivalent English, German and Russian cases.
- [ ] Define bounded semantics for ALL EXCEPT, AGAIN and correction commands
  before implementing them; never replay writes implicitly.
- [ ] Add end-to-end scenarios for each capability, including rejected input,
  changing records, ambiguous names and UI/headless equivalence.

## Separate 1C integration work

- [ ] Design a versioned 1C metadata/data adapter with read-only inspection first.
- [ ] Establish a disposable 1C fixture workflow for BSL/query diagnostics,
  compilation, extension applicability and before/after evidence.
- [ ] Add exchange contract tests for authentication, pagination, invalid data,
  timeouts, retries and idempotency before enabling scheduled delivery.
- [ ] Measure coverage against consecutive anonymized accountant requests,
  including clarification, review, correction and recovery time.

## Completed in this audit

- [x] Verify native accounting, automation, executor, add-on and runtime forms.
- [x] Add creditor thresholds, ISO document-date filters and duplicate identifiers.
- [x] Share object context between topic answers and typed commands.
- [x] Reject recognized unsupported topic scopes and contradictory modifiers.
- [x] Validate budget source dates and amounts while retaining signed reversals.
- [x] Verify multilingual grammar, localization and visible demo workflows.
