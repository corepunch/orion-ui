# Small-batch fulfillment

Pick several open orders in one trip without losing what goes into each order.
The workflow is offline and local. It does not change Ledger records, verify
stock, update orders, create shipments, buy postage, or contact a marketplace
or carrier.

## 1. Select open orders

Export or prepare a local JSON file using the
[example batch](../tests/fixtures/fulfillment/orders.json). Each selected order
needs a unique string `id`, an explicit `"status": "open"`,
`"selected": true`, a short `recipient` (an internal packing alias works), and
`lines` with exact string `sku` and positive whole-number `quantity`.
Do not include customer addresses, access keys, or other marketplace data.
Unselected orders may omit recipient and lines. An explicitly selected closed
or cancelled order aborts the batch. SKUs are case-sensitive, and leading
zeroes are preserved. The file may contain at most 250 orders and 4,000
selected lines. Keep order numbers unique across all included sources.

This is a deliberately narrow local interchange format, not a native Ledger
sales-order selection screen or a marketplace export format. Someone must
confirm the orders are still open when preparing the batch.

## 2. Pick by SKU, then split by order

Run from the repository root with Lua 5.4:

```sh
(lua apps/ledger/tools/fulfillment.lua pick orders.json > pick-list.tmp &&
  mv pick-list.tmp pick-list.html)
```

Open the HTML locally and print. Each SKU appears once, with the total quantity
to pick and the exact split by order ID, including duplicate lines combined
within the same order. Recipients are intentionally omitted from this sheet.

## 3. Pack each order

```sh
(lua apps/ledger/tools/fulfillment.lua labels orders.json > order-labels.tmp &&
  mv order-labels.tmp order-labels.html)
```

The printable cards show the order ID, recipient or packing alias, and the
packing checklist. They are **internal packing labels**, not carrier labels.
Before dispatch, obtain genuine labels through the carrier or marketplace's
authorized process. Item barcode labels are a separate workflow.

Keep the order file and HTML/printouts private and discard them under your
normal retention rules. No automatic status changes or writes occur.

Test: `lua apps/ledger/tests/fulfillment_test.lua`.
