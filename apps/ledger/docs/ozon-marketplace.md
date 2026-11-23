# Ozon Seller preview

The Lua provider reads one Ozon seller account per run. It never writes to
Ozon or Ledger: no settings, stock updates, reservations, listings, orders,
labels, or postings are changed. Its normalized output feeds Ledger's
marketplace preview and the internal pick-list workflow. It has only been
validated against local fixtures, not a live seller account.

## Provider scope

| Read | Ozon Seller API | Ledger result |
|---|---|---|
| FBS postings | `POST /v4/posting/fbs/list` | Posting and order identifiers, current status, offer and quantity per line |
| FBO postings | `POST /v3/posting/fbo/list` | Same order overview; excluded from seller picking |
| Product stocks | `POST /v4/product/info/stocks` | The provider's present and reserved quantities by fulfillment scheme, not by physical warehouse |

These `POST` routes are read-only listing methods. The Lua provider emits
requests only to the fixed HTTPS Ozon Seller API origin. The host transport
owns credentials; the Lua provider inputs, output, tests, and repository contain
no credentials. Use a distinct local account label for each seller credential
pair. Do not use an administrator or write-capable token when a read-only role
is supported. No optional customer, financial, legal, or shipping barcode fields
are requested, and fields returned anyway are not copied into the preview.

Order windows require UTC RFC 3339 `from` and `until_date` values, at most
365 days apart. Availability-only does not require those values. The bounded
reader follows opaque cursors, rejects repeated pages and duplicate posting IDs,
and rejects a stock result that contradicts the item total or runs out of
pages. A failure aborts instead of producing a misleading partial preview.

Ozon's seller offer ID is the exact SKU and stable variant identity. Leading
zeroes are kept. If an offer ID or valid positive quantity is missing on any
line, the complete posting is held back from picking; the numeric remote SKU is
not assumed to be the seller's SKU. Use the Ozon posting number to distinguish
packages that share one Ozon order number. Only complete seller-fulfilled
`awaiting_packaging` postings qualify for picking. FBO, cancelled, already
packed, and digital lines do not generate internal picks. The generated
packing card is not an Ozon shipping label or QR.

When Ozon reports both numbers, its sellable quantity is shown as
`present - reserved`. The raw quantities stay distinct. If reserved exceeds
present, sellable quantity is omitted and the preview flags the row. The
provider's scheme and shipment type do not identify a physical warehouse or
Ledger's accounting stock. Quantity from this snapshot must not be posted or
pushed back to Ozon.

Finance accruals, payouts, returns reconciliation, formal accounting entries,
physical warehouse balances, and official shipping labels are out of scope.
The retired v3 transactions methods are not used; a later finance adapter
should target the current v1 accrual methods and keep gross, fees, refunds,
currency, and payouts distinct.

## References and test

- [Ozon announcement: current FBS/FBO posting methods](https://dev.ozon.ru/news/734-Otkliuchenie-starykh-metodov-Seller-API-dlia-raboty-s-postingami/)
- [Ozon method guide: product stock preview](https://dev.ozon.ru/start/448-Spravochnik-metodov-Seller-API-dlia-Ozon-Logistiki/)
- [Ozon FAQ: interpretation of present and reserved](https://dev.ozon.ru/start/257-FAQ-spravochnik-oshibok-i-sposoby-ikh-resheniia)
- [Ozon announcement: current finance accrual methods](https://dev.ozon.ru/news/783-Izmeneniia-v-metodakh-dlia-raboty-s-finansovymi-otchetami-v-Seller-API/)

From the repository root: `lua5.4 apps/ledger/tests/ozon_marketplace_test.lua`.
