# Workshop operations pilot

The Workshop module runs service-work orders without creating a new executable
or writing Ledger accounts. It is an opt-in local command-line pilot, not a
production multi-user repair shop. It covers a first slice of #356, #357, #358
and #361. It also exports a reviewable invoice intent for #360, not an invoice.

## Local operator workflow

Use Lua 5.4 from the Orion UI repository root. First create an empty journal in
a private directory with access permissions restricted to the operator. The
journal carries customer references and work history, so back it up and handle
it like other customer data.

```sh
umask 077
lua5.4 apps/ledger/tools/workshop.lua init > workshop.json
```

Write a command to a separate JSON file. For every new command set
`expected_revision` to the revision from `show` or the preceding receipt.
Use a new request ID for a change, and resend the same request unchanged after
an uncertain result. `at_min` is a nondecreasing UTC epoch minute. The value
below is an example, not a runtime clock.

```json
{
  "request_id": "intake-0001",
  "expected_revision": 0,
  "actor": "operator-1",
  "at_min": 29937600,
  "command": "vehicle.register",
  "args": {
    "id": "car-1", "owner_ref": "customer-1",
    "plate": "B XR 100", "jurisdiction": "DE-BE",
    "vin": "WVWZZZ1JZ3W386752", "make": "Volkswagen", "model": "Golf",
    "year": 2003, "odometer_km": 153492
  }
}
```

Run with a **single writer**. Write to a separate file or a temporary file in
the same directory, then replace the journal on success. Never redirect output
directly into the input journal. Keep the previous copy behind ordinary
backup policy.

```sh
next=$(mktemp ./workshop.next.XXXXXX)
if lua5.4 apps/ledger/tools/workshop.lua apply workshop.json command.json > "$next"; then
  mv "$next" workshop.json
else
  rm -f "$next"
fi
lua5.4 apps/ledger/tools/workshop.lua show workshop.json
lua5.4 apps/ledger/tools/workshop.lua history workshop.json car-1
lua5.4 apps/ledger/tools/workshop.lua report workshop.json 29937600 29939040
lua5.4 apps/ledger/tools/workshop.lua report-csv workshop.json 29937600 29939040 > activity.csv
# After a repair-1 work order exists:
lua5.4 apps/ledger/tools/workshop.lua print workshop.json repair-1 > repair-1.html
```

The CLI writes receipts to stderr and the successor JSON journal to stdout.
An unchanged retry returns the original receipt and an unchanged journal.
Stale revisions and same-ID changes fail without modifying the input.
The app replays and validates the journal on every open. It caps journals at
1,000 accepted commands or 1 MiB including the trailing newline. The revision
check is against the journal read by the process; it is **not** a file-locking
or cross-process compare-and-swap protocol.

`find JOURNAL QUERY` accepts `owner_ref`, `vin`, or the pair `plate` and
`jurisdiction`, with all specified filters combined. An owner can have
several vehicles. VIN is optional and unique after uppercasing. Plate lookup
ignores ASCII case, spaces and hyphens; other bytes are matched exactly.
Duplicate plate matches are returned for review, not merged. Re-registration
and odometer corrections retain history.

## Command and data model

| Command | Arguments |
| --- | --- |
| `vehicle.register` | `id`, `owner_ref`, `make`, `model`, `year`; optional `vin`, `plate` + `jurisdiction`, `odometer_km` |
| `vehicle.registration` | `id`, `reason`, optional `plate` + `jurisdiction`; omit both to clear |
| `vehicle.odometer` | `id`, `km`; `reason` required if a reading decreases |
| `resource.register` | `id`, `name`, `kind` (`bay`, `worker` or `equipment`) |
| `resource.block` | `id`, `resource`, `start_min`, `end_min`, `reason` |
| `resource.unblock` | original block `id`, `reason` |
| `order.open` | `id`, `vehicle`, `complaint` |
| `order.quote` | `id`, `currency`, `lines`: `kind` (`part` or `labor`), `reference`, `description`, integer `quantity`, integer `unit_minor` |
| `order.approve` | `id`, current `estimate_version` |
| `order.schedule` | `id`, `start_min`, `end_min`, `resources`; repeat to reschedule before start |
| `order.start` | `id` |
| `parts.record` | `id`, approved part `reference`, actual integer `quantity`, `reason` |
| `time.log` | order `id`, `entry`, assigned `worker`, `start_min`, `end_min`, `billable_min` |
| `time.correct` | order `id`, original `entry`, new `replacement`, `worker`, `start_min`, `end_min`, `billable_min`, `reason` |
| `order.cancel` | `id`, `reason`; releases its appointment |
| `order.complete` | `id`, `inspection`; produces one stable invoice-intent reference |

Bookings are half-open intervals in UTC with exclusive capacity per resource.
Adjacent reservations are permitted. A booking also cannot span a manual
block, and a stale or failed reschedule leaves the prior booking unchanged.
Supply UTC, converting from the desired local time before invoking the tool;
it does not decide ambiguous DST times or recurring hours.

Time rows distinguish recorded from billable minutes. Corrections before
completion replace the active entry without deleting the original. Reports
clip intervals to the requested UTC period and distinguish appointments,
manual blocks, and recorded worker time. `open_orders_now` is a point-in-time
count at the current revision, not a historical count. The operational module
has no wage, payroll or statutory reporting fields.

The printable page shows the order, current vehicle identifiers, approved or
unapproved estimate and reported material usage. It is not a statutory
document. CSV exports activity by resource; missing worker time for a
non-worker resource is left blank.

The invoice intent keeps approved prices, currency and observed parts separately.
It needs accountant review even when there is no discrepancy. Part quantities
do not reserve, consume or return Ledger stock. Missing actual usage does not
mean zero. Do not import estimates as stock or post intents automatically.

## Scope still open

This pilot does not authenticate an actor, expose a Ledger/Lodge menu or write
to remote services, manage multi-user persistence or payroll permissions,
compute margin from native costs, generate statutory invoices, or publish
event/outbox messages. Role enforcement, live inventory reservation, calendar
UI, payroll access tests and validation with an
independent workshop remain open. The journal and tested state machine are
intended as an adapter boundary for those later host integrations.

Tests:

```sh
lua5.4 apps/ledger/tests/workshop_test.lua
sh apps/ledger/tests/workshop_cli_test.sh lua5.4
```
