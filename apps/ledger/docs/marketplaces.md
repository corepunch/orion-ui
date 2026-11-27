# Read-only Ozon and eBay workflows

## In the Ledger desktop app

Open **File → Marketplaces**, choose Ozon or eBay, enter a short local account
label, and choose **Orders**, **Availability**, or both. The dialog supplies
a seven-day UTC time range and never contacts a seller until you click
**Load Marketplace Data**. Orders and provider availability appear separately.
Review held orders and then choose **Preview Pick Sheet**. **Save Pick Sheet**
writes an HTML file only after you choose the destination. The pick sheet
keeps order splits and excludes held orders. **Save Orders Snapshot** exports
the sanitized order data to a user-selected JSON file for the offline
workbench. It does not export provider availability, credentials or customer
addresses. For batches above 1 MiB, narrow the date range. Ledger and the
marketplaces are not modified. Cancel or close drops the in-memory result.

- **Ozon:** enter the numeric Seller Client ID and an API key with read access.
- **eBay (advanced):** paste a short-lived seller OAuth **user access token**
  and select Production or Sandbox. This is not a one-click eBay connection:
  the app does not issue, store or renew OAuth credentials. See the OAuth
  authorization instructions below.
- Both use the masked credential field; Ledger retains no credentials in
  settings, a local header file, or the infobase. Credentials are consumed
  for the request and cleared. Pasting does not change your system clipboard.

The packaged integrations are visible automatically in the desktop menu;
credentials are never preloaded and network access is always user-initiated.
Only the two bundled, reviewed provider adapters receive the narrow
network capability. This does not change how external Lua plugins load.
This desktop connection is supported on macOS and Linux with a compatible
system libcurl. Linux paste uses X11/XWayland; native Wayland clipboard paste
is not available. Windows and other builds report that the connection is
unavailable. The desktop path does not require a shell, CLI or header file.

## Optional local CLI

Run the Lua 5.4 CLI from a checkout on Linux or macOS with curl installed.
It fetches one seller account at a time. Fetching does not schedule a job, write
to the Ledger infobase, send data to an intermediary, or change a marketplace.
The separate offline workbench below can retain a sanitized copy locally.
No live seller account was used to test either path.

## CLI credentials and eBay OAuth

Use a separate account/key authorized by the seller, with only the corresponding
read permissions. Keep credentials outside the repository and configuration
JSON. Create the private file with a local editor under `umask 077`, set mode
`600` (or `400`), and point to it with an absolute path:

- eBay: set `ORION_EBAY_HEADERS=/absolute/private/path`. File: one line,
  `Authorization: Bearer YOUR_USER_ACCESS_TOKEN`. Use a **seller user** token
  issued through the [OAuth authorization code grant](https://developer.ebay.com/api-docs/static/oauth-authorization-code-grant.html)
  for the matching Sandbox or Production keyset. Scopes: `sell.fulfillment.readonly`
  and `sell.inventory.readonly` under `https://api.ebay.com/oauth/api_scope/`; for
  orders or availability alone you need only the relevant scope. Obtain and
  refresh the token through your registered OAuth client (Ledger does not
  request consent, store a refresh token, or renew it). A client-credentials
  application token is not a seller user token.
- Ozon: set `ORION_OZON_HEADERS=/absolute/private/path`. The two lines are
  `Client-Id: YOUR_NUMERIC_CLIENT_ID` and `Api-Key: YOUR_READ_KEY`.
  See the [Ozon provider notes](ozon-marketplace.md) for endpoints and settings.

Only these headers are accepted, with LF line endings. The command sends them
to fixed HTTPS provider hosts/endpoints, disables redirects/proxies, and does
not place credentials in command arguments or print provider error bodies.
Do not paste tokens into command lines or share the files; revoke if exposed.
The output omits buyer names, addresses, emails, payment data and raw API
responses. Order IDs remain commercial information; keep downloaded output
private (for example, use `umask 077` before shell redirection).

## Snapshot and pick list

Example `market.json` (use an actual date window; required for Ozon orders):

```json
{
  "account": "my-shop",
  "environment": "production",
  "mode": "both",
  "from": "2026-11-20T00:00:00Z",
  "until_date": "2026-11-23T00:00:00Z",
  "page_size": 100,
  "max_pages": 20
}
```

```sh
umask 077
lua apps/ledger/tools/marketplace.lua ebay snapshot market.json > ebay.json
lua apps/ledger/tools/marketplace.lua ozon snapshot market.json > ozon.json
lua apps/ledger/tools/marketplace.lua ebay pick-json market.json > ebay-picks.json
lua apps/ledger/tools/fulfillment.lua pick ebay-picks.json > pick-list.html
# Or render directly from the selected account:
lua apps/ledger/tools/marketplace.lua ozon pick-html market.json > ozon-pick.html
lua apps/ledger/tools/marketplace.lua ozon labels-html market.json > ozon-packing.html
```

The snapshot has source/account, orders/lines/status, hold reasons, warnings
and a separate availability section with an explicit quantity basis. Supported
modes are `orders`, `availability` or `both`. Picks fetch orders only. Review
held orders and compare the first run to the seller portal before printing.
Overlapping/repeated pages or inconsistent pagination cause the run to fail
without JSON or HTML output. Use a fixed window and narrow it if the page or
250 eligible order batch limit is reached.

The pick pipeline groups exact SKU, variant and supplied barcode, keeps the
quantity split by namespaced order ID, and reuses the existing Ledger print
workflow. These IDs include provider, environment and the local account alias;
choose a different alias for each seller. A missing or ambiguous line holds
the whole order. If a source has no usable barcode, no barcode is invented.
The stateless pick commands still include every eligible order in the selected
window. For repeat picking, use the offline workbench instead. Internal
packing labels are not carrier labels.

## Offline marketplace workbench

For local operator use on macOS or Linux. Use a private workspace directory
per seller account and environment. The workbench imports a complete
snapshot from the desktop export or fetch command above; it writes no Ledger stock or accounting
records and has no network or fulfillment-confirmation capability. The
desktop marketplace dialog remains read-only.

```sh
umask 077
mkdir shop-work
workbench=apps/ledger/tools/marketplace_workbench.lua
lua "$workbench" shop-work sync ebay.json
lua "$workbench" shop-work report
# Explicit mapping: seller SKU, exact variant (empty when absent), Ledger item ID, barcode.
lua "$workbench" shop-work map 0007 '' item-0007 0000123
lua "$workbench" shop-work pick-html > shop-picks.html
# Use one durable, unique event ID per physical scan. On retry, repeat the ID.
lua "$workbench" shop-work scan 27-10001-00001 0000123 scan-session1-001
lua "$workbench" shop-work pack 27-10001-00001  # only after all units cross the scan step
# Verify the order/package in the seller portal before importing its PDF.
lua "$workbench" shop-work attach-label 27-10001-00001 TRACK-01 portal-label.pdf
lua "$workbench" shop-work reprint-label 27-10001-00001 TRACK-01 > reprint.pdf
```

- Importing the same contents again does not duplicate or reset a pick.
  The fetch CLI includes capture-start time; supply the original Unix capture
  time when importing an older snapshot without that field. Older observations
  do not overwrite newer ones for the same order. If differing snapshots have
  the same capture time, import a fresh capture instead. This is a local
  observation version, not a provider revision token or an automatic cursor.
  An order missing from a date window is never deleted or assumed cancelled.
- Unmapped SKUs, barcode disagreements and ambiguous scans do not change
  quantities. Leading zeroes are significant. The pick sheet prints only
  remaining units of resolved open orders. Input is a decoded scanner value;
  no camera hardware is controlled by this tool.
- Explicit cancelled status stops picking and reprints. Other held statuses
  cannot be advanced. A changed provider order that already has local work
  requires review; the report retains the earlier picks. Review remaining
  stock and retire old labels before using `restart ORDER_ID stock-reconciled`
  on an eligible changed order. This is an operator acknowledgement, not a
  Ledger reservation, release or inventory adjustment.
- Carrier PDFs come from the operator's marketplace or carrier portal, not
  from an internal packing-label generator. The original bytes (up to 128 KiB)
  are associated with one order and package ID, with no silent replacement or
  reassignment. Format checks cannot prove the issuer or order association.
  Import only trusted files that you have verified against the portal.
  Unlike normalized order snapshots, these PDFs may contain personal data.
- This is a small offline workspace (maximum 1 MiB including labels), not a
  shared database. Keep it on a local filesystem and back it up privately.
  Concurrent writes are rejected and the new file replaces the old atomically.
  If an interrupted command leaves `.writer/`, remove it only after confirming
  no writer is running and checking that the previous state is still intact.
- Fees, returns, payouts and margins are reported unknown; no finance feed is
  connected. Provider availability still does not imply Ledger stock.
  Native stock reservation/valuation, a camera UI, live carrier integration,
  and any provider writes require separate validation.

## eBay coverage

- [Fulfillment getOrders](https://developer.ebay.com/develop/api/sell/fulfillment_api/order/getOrders):
  read checkout orders, optionally with an explicit creation-date window.
  Only completely unstarted, paid and uncancelled seller-shipped orders with
  usable line IDs, SKU and quantity are pickable. Digital, pickup, previously
  processed and uncertain orders remain visible as held. If no window is
  supplied the documented default history applies.
- [Sell Inventory](https://developer.ebay.com/develop/api/sell/inventory_api):
  retrieve seller inventory records and show shipping totals and location /
  pickup allocations separately. The counts may overlap; never sum them or
  use them as Ledger warehouse counts, reservations or available-to-promise.
  [Existing Seller Hub listings absent from the Inventory API](https://developer.ebay.com/support/knowledge-base/5210)
  can still produce orders, but cannot be counted with this endpoint.
- Seller refunds, tracking, shipment status, listing/stock updates and
  accounting are not connected. Finances are deferred: an order total is not
  a payout or fee statement, and [eBay Finances](https://developer.ebay.com/api-docs/sell/finances/overview.html)
  can require [digital signatures](https://developer.ebay.com/api-docs/static/digital-signatures.html)
  for EU/UK sellers. Ozon accrual/reconciliation is also not implemented.
