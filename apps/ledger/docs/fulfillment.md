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
`lines` with exact string `sku`, optional exact string `variant`, and positive
whole-number `quantity`. Use the same full human-readable characteristic
(such as `Navy / 105 cm / straight handle`) on every line of that variant.
Omit `variant` for legacy or truly variant-free goods; missing variants form a
separate group and are never merged with a named variant.
Do not include customer addresses, access keys, or other marketplace data.
Unselected orders may omit recipient and lines. An explicitly selected closed
or cancelled order aborts the batch. SKUs and variants are case-sensitive,
and leading zeroes are preserved. Test your exports for stable variant text. The file may contain at most 250 orders and 4,000
selected lines. Keep order numbers unique across all included sources.

This is a deliberately narrow local interchange format, not a native Ledger
sales-order selection screen or a marketplace export format. Someone must
confirm the orders are still open when preparing the batch.

## 2. Pick by SKU and variant, then split by order

Run from the repository root with Lua 5.4:

```sh
(lua apps/ledger/tools/fulfillment.lua pick orders.json > pick-list.tmp &&
  mv pick-list.tmp pick-list.html)
```

Open the HTML locally and print. Each SKU/variant combination appears once,
with the full variant before the total and order IDs. Duplicate lines within
one order combine only when both values match. Recipients are intentionally
omitted from this sheet.

The fixture covers one umbrella SKU, two visually similar variants and three
orders. Pick three Navy / 105 cm / straight handle units, then split one to
O-101 and two to O-102. Pick four Navy / 90 cm / curved handle units separately
for O-105.

## 3. Pack each order

```sh
(lua apps/ledger/tools/fulfillment.lua labels orders.json > order-labels.tmp &&
  mv order-labels.tmp order-labels.html)
```

The printable cards show the order ID, recipient or packing alias, and the
packing checklist with the same full variant shown on the pick list. Match by
order ID and variant, not by the order the cards came out of the printer.
They are **internal packing labels**, not carrier labels.
Before dispatch, obtain genuine labels through the carrier or marketplace's
authorized process. Item barcode labels are a separate workflow.

Keep the order file and HTML/printouts private and discard them under your
normal retention rules. No automatic status changes or writes occur.

Test: `lua apps/ledger/tests/fulfillment_test.lua`.

## Why separate picking from packing

A [1C warehouse discussion](https://forum.mista.ru/topic/903131/tail) reports
labels getting shifted by one order (reply 144). In reply 163 a warehouse
operator describes their own process: print the batch grouped by product, put
the full product characteristics before the order labels, then handle one
group at a time. This workflow applies those checks to the pick sheet and
internal packing cards. It does not generate marketplace QR codes or emulate
their label roll, and no physical printer or handheld scanner was tested.
