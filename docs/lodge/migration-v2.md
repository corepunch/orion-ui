# Lodge database versions 2–5

Version 2 adds reservation history and signed payment events. Before opening an
existing hotel with the new version, close Lodge and make a separate copy of
`hotel.xml` in a safe location. Use one process per hotel data directory.

On the first successful open, Lodge records each existing reservation's paid
amount as an `opening_balance` event and its current details as an opening
snapshot. This is an imported balance, not a claim about the dates, methods or
number of historical payments. Later payments and refunds are recorded as
signed minor-unit events. Reopening does not create another opening event.
Existing hotels have no country or currency automatically inferred. Before new
reservation or payment changes, an operator must explicitly adopt Germany/EUR
or Russia/RUB in Hotel setup. Existing amounts are interpreted in that currency
without conversion, and the choice cannot be changed. Back up and verify the
old amounts first; currency conversion and historical exchange rates are not
supported. Country selection does not set tax treatment.

The framework migrates supported older XML in memory and publishes the
updated file only after initialization succeeds. Its save keeps the previous
file as `hotel.xml.bak`; subsequent saves may replace that backup, so retain
the independent copy. If an upgrade fails, do not edit or replace the original
while Lodge is running. Preserve the file and diagnostic output, then restore
from the independent copy with the application closed. A newer-format file is
rejected by a runtime that enforces schema version compatibility and supports
only an older version; older releases without this check should not be used on
the upgraded file.

Version 3 adds room cleaning status. Existing rooms start as needing inspection,
so staff must verify and mark each room clean before check-in. Check-out marks a
room dirty. The status does not record cleaning assignments or inspection history.

Version 4 adds optional per-night prices. Existing reservations retain their
previous copied base rate; no nightly overrides are invented during migration.
It also adds groups, operational day-close records and actor/date fields for
new activity. Earlier audit actors remain `legacy-unknown`; imported opening
balances are labeled `migration`. Older payment events have no posting date,
so they cannot be attributed to a daily payment report.

Version 5 adds the paid cancellation count and amount to new day closes.
Earlier closes did not capture those values, so their history shows Unknown;
the upgrade does not reconstruct them from current reservations.

The reception payment window submits a payment or refund using a key retained
for retries in that window. Integrations may use
`Hotel.payment(id, amount_minor, request_key)` with their own stable retry key.
If a save reports failure after the primary file was replaced but directory sync
failed, the outcome is uncertain: inspect the database before starting a new
request. Lodge does not yet provide payment-provider integration,
user identity, authorization, tax accounting, or multi-terminal transactions.
