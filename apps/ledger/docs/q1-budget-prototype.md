# Q1 budget assistant prototype

The assistant can build a read-only accrual income/expense worksheet from the
RU/DE 2026 demo charts, then edit proposed amounts without posting transactions.
This is a focused local command flow, not unrestricted language understanding.

## End-to-end example

Start Ledger on a disposable demo infobase (`--assistant --lang ru`).

1. `Сверстай бюджет за первый квартал`
2. The assistant asks `За какой год?` It does not infer a year from today's date.
3. `2026`
4. The worksheet shows January, February and March actuals, separate blank
   proposals, category/month/quarter totals, coverage warnings and source IDs.
5. `план 90.01 1 100000` enters a **user-supplied** January gross revenue proposal.
   Actuals are unchanged. No other month's/category's amount is inferred.
6. `план 26 1 0` explicitly budgets zero overhead for January. This is different
   from leaving that cell unset. These are examples of input, not recommendations.
7. Source rows open existing documents through Ledger's normal forms. The first
   six distinct documents are linked; all included source sides are in the text.
8. Existing requests such as `покажи клиентов` still work.

The scrollable worksheet text is editable and can be copied. Free text edits
are document drafting only: they do **not** change the structured figures or
recalculate totals. `план ACCOUNT MONTH AMOUNT` updates structured proposals and
regenerates text/totals. Closing the bubble retains the text. A new budget request
starts a new worksheet. There is no durable budget storage/export command yet;
copy the draft before closing Ledger. This prototype does not save proposals to
the infobase, and the worksheet is a snapshot rather than a live report.

## Result on the repository's Russian demo

Native integration test output, freshly reposted `ru-2026/demo.xml`, Q1 2026,
accounting currency RUB. All proposed values are **unset** initially.

| Category / account | January actual | February actual | March actual | Q1 actual |
|---|---:|---:|---:|---:|
| Gross revenue / 90.01 | 0.00* | 0.00* | 73,938.00 | 73,938.00 |
| Output VAT deduction / 90.03 | 0.00* | 0.00* | −13,038.00 | −13,038.00 |
| Other income / 91.01 | 0.00* | 0.00* | 150.00 | 150.00 |
| **Income after VAT deduction** | **0.00*** | **0.00*** | **61,050.00** | **61,050.00** |
| General overhead / 26 | 0.00* | 0.00* | 416,540.00 | 416,540.00 |
| Selling expenses / 44 | 0.00* | 0.00* | 117,180.00 | 117,180.00 |
| Cost of sales / 90.02 | 0.00* | 0.00* | 51,250.00 | 51,250.00 |
| Selling expenses / 90.07 | 0.00* | 0.00* | 0.00 | 0.00 |
| Management expenses / 90.08 | 0.00* | 0.00* | 0.00 | 0.00 |
| Other expenses / 91.02 | 0.00* | 0.00* | 1,100.00 | 1,100.00 |
| **Expenses** | **0.00*** | **0.00*** | **586,070.00** | **586,070.00** |
| **Income less expenses** | **0.00*** | **0.00*** | **−525,020.00** | **−525,020.00** |

\* No mapped source entries; coverage is unknown. This is not evidence that
January and February had no economic activity. These totals describe this demo,
not a forecast, tax return, complete profit statement or cash-flow budget.

Examples of traceable sources:

| Date | Document ID / number | Account / corresponding account | Contribution |
|---|---|---|---:|
| 2026-03-10 | 7 / РТ-0001 | 90.01 / 62.01 | revenue 68,320.00 |
| 2026-03-10 | 7 / РТ-0001 | 90.03 / 68.02 | income deduction −12,320.00 |
| 2026-03-06 | 6 / ПУ-0001 | 26 / 60.01 | expense 60,000.00 |
| 2026-03-31 | 14 / НЗ-0001 | 26 / 70 | expense 150,000.00 |
| 2026-03-31 | 16 / ЗМ-0001 | 91.01 / 60.21 | other income 150.00 |

The full emitted worksheets and included source sides are reproduced by the
native suite as `build/budget-ru-2026.txt` and `build/budget-de-2026.txt`.

## Calculation and mapping

`budget.lua` holds the reviewed, explicit configuration profiles. Categories
are accounts, using names supplied by `ledger.account`, not name heuristics or
an LLM. Unknown configurations/missing mapped accounts fail before any query.

RU: income 90.01, 90.03 (negative output VAT), 91.01; expenses 26, 44, 90.02,
90.07, 90.08, 91.02. DE: income 2650, 2660, 8300, 8400; expenses 2150, 3300,
3400, 4120, 4130, 4210, 4830, 4855, 4930, 4950. These are demo profiles, not
universal chart coverage. In particular RU production cost capitalization,
profit taxes and arbitrary custom accounts are not classified automatically.

The query is inclusive January 1 through March 31 of the requested year.
`ledger.movements` returns posted entries only, with account children included.
Roots must not overlap. Income is credit less debit; expenses are debit less
credit. RU VAT therefore appears as a negative income adjustment. User-entered
VAT proposals are nonnegative amounts, displayed/contributed as deductions.
All other proposals are nonnegative and in accounting currency. No exchange
rate is inferred: accounting entries already contain base-currency amounts;
this does not claim native-currency forecasting support.

Each debit/credit amount is rounded to cents at the boundary and sums use integer
cents. Ledger's existing floating-point representation remains the source;
this does not introduce an arbitrary-precision decimal accounting engine.

Transfers between mapped accounts and closing to RU 00/90.09/91.09/99 or DE 9000
are excluded on both mapped sides. Otherwise overhead transferred to 90.08
would appear twice. Economic depreciation/revaluation entries generated by a
closing document remain included: filtering every closing *document* would
incorrectly remove them. Included and excluded source-side records retain date,
document ID/number, account, corresponding account, signed contribution and
content. This is an in-memory audit trail, not a durable audit log.

Proposal totals are reported only when every contributing category cell is
explicitly set; missing is never silently summed as zero. Actual zeroes remain
numeric zeroes with separate month coverage warnings.

## Natural language → validated operation

The local Russian grammar produces only this language-neutral data value:

```lua
{operation='budget.q1', version=1, year=2026}
```

`Budget.validate` rejects unknown keys, unsupported operation/version, missing or
invalid years. `Budget.run` receives a capability table containing only
`configuration`, `account`, `movements`. It cannot reach posting, menu commands,
settlement actions, writable models or the trusted automation runtime through
that interface. Budget-topic requests that do not match the full grammar are
refused without falling through to broad topic matching/menu search.

The existing trusted `ledger.automation` graph/recipe registry lives in the
posting-hook Lua state, while the assistant uses the sandboxed add-on state.
Exposing that entire runtime would also expose write/import capabilities.
For this prototype the shared architectural pattern is a typed validated data
operation with a narrowly injected read API. No generic bridge to trusted Lua
or arbitrary graph execution is added. A future shared read-only recipe should
move the calculation module into a common sandbox-compatible module and register
an XML-described read operation in both hosts; it should not duplicate formulas.

## Optional LLM planner evaluation

An LLM is useful for paraphrases and asking clarifying questions, but it has no
role in arithmetic, account classification, forecasts or database execution.
The typed plan above is the integration boundary. An optional host-side planner
could return JSON constrained to that schema; after decoding, `Budget.validate`
remains mandatory, and the proposed year/operation must be displayed for user
confirmation before `Budget.run`. Unset year must request clarification.

The assistant sandbox currently has no HTTP/API-key access. No provider,
credential UI, network calls or LLM dependency is added in this prototype.
The planner design is evaluated architecturally and with adversarial plan
validation tests, not claimed as a tested live model integration. Send only the
user's request and operation schema to a planner; do not send transaction rows.
Neither model-produced Lua/SQL nor returned amount/account/filter fields are
accepted. For the single demonstrated intent the local grammar is cheaper,
reproducible and sufficient; a provider adapter is deferred until broader
paraphrase coverage justifies the network/privacy/confirmation flow.

## Validation

```sh
make share build/bin/test_ledger_assistant_test
EGL_PLATFORM=surfaceless build/bin/test_ledger_assistant_test # Linux headless
lua apps/ledger/tests/assistant_budget_test.lua
python3 apps/ledger/tests/assistant_vocabulary_test.py
python3 apps/ledger/tests/assistant_locale_test.py
```

The native suite includes the portable budget tests plus real RU/DE demo posting,
assistant input, missing-year dialogue, full long report delivery, editing a
proposal, refusal of posting requests, unchanged document/entry counts and
existing assistant behavior. The native tests also emit the full worksheets.
The new worksheet required fixing two general text limits: MultiEdit now grows
its buffer, and the script page passes/reads complete multiline strings rather
than truncating at 1024 bytes. Both paths are exercised by the native worksheet
round trip and collapse/expand checks.
