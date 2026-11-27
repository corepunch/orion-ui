# Bank and document exchange review

Ledger's operator-side `tools/provider_exchange.py` fetches read-only provider
evidence. It is separate from the local ClientBank file preview. It does not
post, send payments, alter the infobase, or sign or send EDI documents.

## T-Bank booked RUB transactions

Use a dedicated token with the provider's account-operation read scope.
[T-Bank's statement endpoint](https://developer.tbank.ru/docs/api/get-api-v-1-statement)
accepts an inclusive `from` and an exclusive `to`, both UTC. Begin in the sandbox:

```sh
python3 apps/ledger/tools/provider_exchange.py bank --environment sandbox \
  --account ACCOUNT --from 2026-11-01T00:00:00Z --to 2026-12-01T00:00:00Z \
  --output statement-review.json
```

The CLI prompts without echo, sends only HTTPS GET to the selected provider
endpoint, refuses redirects and does not use proxy environment variables.
Use `--environment production` explicitly only when reviewing company-owned
data. A successful command creates a new output file with mode 0600 on POSIX;
use a secure owner-only directory and keep its contents out of Git or shared
folders. On Windows, filesystem ACLs must protect the directory.
On failure the command does not publish a partial report. A filesystem
supporting hard links is required for atomic, no-overwrite output.

The adapter follows the provider's cursor. Confirmed transactions are keyed by
provider, account and operation ID; identical repeats within one report are
collapsed, and differing records for the same identity fail closed. This
does not persist a sync checkpoint or authorize a later posting. Account
amounts are stored in integer kopecks even when a card operation has a
different original currency. Do not treat the statement as an opening or
closing balance report. Keep its UTC timestamp and verify the appropriate
local posting date before using the row.

An optional `--invoices unpaid.json` shows exact **candidates** using a
user-prepared review snapshot. It does not read or alter Ledger's database:

```json
[
  {
    "ledger_id": "ledger-invoice-id",
    "counterparty_inn": "1234567890",
    "payment_reference": "INV-25",
    "outstanding_minor": 14455,
    "currency": "RUB"
  }
]
```

Only a distinct incoming transaction with exact outstanding amount, INN and a
standalone reference in the payment purpose can be a unique candidate. The
bank's own document number is not an invoice number. Multiple rows claiming
one invoice, multiple invoices for one row, partial payments and unresolved
references remain for operator review. Recheck the invoice, remaining balance,
organization and date in Ledger before recording any payment.

## Diadoc incoming documents

Use an organization box to which your dedicated read token grants access.
Use a provider test box first. The [incoming-document list](https://developer.kontur.ru/doc/diadoc-api/http/GetDocuments)
returns status metadata and the provider's message and entity identifiers.

```sh
python3 apps/ledger/tools/provider_exchange.py edi-inbox \
  --box BOX_ID --output inbox-review.json
python3 apps/ledger/tools/provider_exchange.py edi-document \
  --box BOX_ID --message MESSAGE_ID --entity ENTITY_ID --output reviewed-document.bin
```

The second command uses [GetEntityContent](https://developer.kontur.ru/doc/diadoc-api/http/GetEntityContent)
and saves bytes unchanged, under the operator's chosen filename. Listing
deduplicates each message/entity pair and does not fetch payloads automatically.
Provider status is not local verification of a signature. Downloaded content
is untrusted: review it with an appropriate reader and never run it as code.

The provider methods are contract-tested with synthetic responses:
`python3 apps/ledger/tests/provider_exchange_test.py`. Live authenticated
sandbox calls, persistent cross-run import, native in-app matching, partial
settlement, and EDI sending remain unverified or out of scope. No operation
here replaces the standard Ledger posting flow.

## Review partial payments, advances and refunds

Standard Ledger plugins expose `ledger.invoice.review_settlements(snapshot,
events)` to trusted Lua callers. Supply an accountant-selected invoice
snapshot (`invoice_ref`, `configuration`, `currency`, `tax_scheme`,
`tax_date`, integer `total_minor`) and an ordered event array. Each event
must identify the same invoice and currency, its `kind` (`payment` or
`refund`), `source`, `request_key`, `actor`, calendar `date`, and positive
integer `amount_minor`. A refund also identifies its earlier payment via
`payment_source` and `payment_key`. Exact repeated events are ignored;
changed evidence under the same identity and refunds exceeding the original
payment's remainder are rejected.

The result breaks out paid, refunded, applied, outstanding and advance
minor units and preserves the source evidence for accountant review. It
does not generate an invoice number, store an event, create a draft, change
stock, or post journals. The first boundary accepts only the ordinary
scheme for `ru-2026`/RUB and `de-2026`/EUR snapshots; it does not validate
country-of-supply, tax compliance or infer invoice identity from a bank row. Operational fulfillment
and Lodge checkout are not coupled to Ledger through this evaluator.
