# What 1C UNF users ask for

These qualitative examples are design inputs, not a market ranking. They
describe features or friction from specific users. These local tools trade
live automation for an explicit, reviewable input and print/reconciliation
step.

| User feedback | What Ledger provides here | Remaining gap |
| --- | --- | --- |
| [1C label layout question](https://mista.ru/topic/903199): margins and print templates matter. [UNF mobile feedback](https://appvisor.ru/app/ios/1sunf-v-oblake-59163/) reports reliance on camera scanning. | [Internal Code 128 labels](item-labels.md) with exact roll size, paper previews and visible SKU. Independent raster/PDF decode checked two payloads. | Physical printer/scanner and calibration validation; phone scanner UI and standardized carrier/regulated formats are out of scope. |
| Small production needs a simple photo/catalog card with article, dimensions, variations and quantities. | [Local product card](product-card.md) uses a selected catalog ID, an optional local photo and explicitly sourced dimensions/variant/quantity. Printable to PDF. | No persistent variant inventory, stock adjustment, camera or image library; quantity is declared snapshot data, not Ledger valuation. |
| [Fulfillment practitioners](https://forum.mista.ru/topic/903131/tail) describe swapped neighboring labels and grouping by product/variant. [A UNF 3.0 report](https://forum.mista.ru/topic/902317) illustrates barcodes attached to characteristics, not just an article. | [Local pick and packing workflow](fulfillment.md) groups exact SKU + variant + supplied barcode, retaining the original orders and quantities. Fixtures verify two variants of one article stay separate across three orders and on reprint. | No automatic scan-to-print, order QR, marketplace/carrier API, tracking or stock reservation; verify exported order data before printing. |
| [UNF user reviews](https://crmindex.ru/products/1c_yhf/reviews) value goods/money visibility, sales analysis and flexible reports; others find costs hard to trace. | [Read-only OData balance preview](unf-odata.md) exposes source register, dimensions, timestamp and units. [ClientBank preview](bank-statement-preview.md) keeps references and reconciliation warnings visible. | No warehouse valuation or automatic payment application; verify live OData on a test tenant. Ordinary Ledger settlement/refusal behavior remains separate. |
