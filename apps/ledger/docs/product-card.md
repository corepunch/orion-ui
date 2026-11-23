# Product cards for small workshops

Print a one-page product card from a real Ledger catalog item: name, article
(SKU), optional photo, size, variant, and dated quantity. This covers the
catalog/print part of the [small workshop request](https://mp-forum.ru/threads/ishchu-programmu-sklad-tovarnogo-ucheta.14255/)
without inventing stock transactions. It is a local Lua export, not a new
Ledger screen, stock register, or PDF engine.

## Export and print

1. Save a read-only Ledger catalog request as `items-request.json`:

   ```json
   {"command":"catalogs.list","actor":"product-card-export","role":"auditor","args":{"table":"items","offset":0,"limit":200}}
   ```

   Run this from the checkout with your own local Ledger book (adjust `--config`
   as appropriate):

   ```sh
   build/bin/ledger-run --config ru-2026 --data /path/to/book --request items-request.json > items.json
   ```

   Find the desired item ID in `result`. For larger catalogs, increase `offset`
   in the request until you export the page with the desired ID.
   Item name and SKU come from the catalog, not from the optional card details.
   Two names or SKUs may coincide, so select by the local record ID.

2. Optionally save additional presentation-only details as `card-details.json`:

   ```json
   {
     "language": "ru",
     "size": "90 × 60 × 35 мм",
     "variant": "Синий, с ручкой",
     "stock": {
       "quantity": "12",
       "unit": "шт.",
       "source": "1С:УНФ, основной склад",
       "as_of": "2026-11-23"
     }
   }
   ```

   All details are optional; for `stock` all four fields are required.
   Quantity is a signed decimal string, for example `"12.005"`. A supplied
   quantity is a display snapshot, not a computed or editable Ledger
   balance. A card without a known snapshot leaves quantity blank rather
   than claiming zero. Keep source and date accurate. Language defaults to
   English; Russian and German are also available. Keep one details file
   per item/variant. When variants need distinct stock identity, give each
   its own existing item ID/SKU rather than putting multiple goods under
   one printed code.

3. Generate a print-ready HTML file using Lua 5.3 or 5.4:

   ```sh
   lua apps/ledger/tools/product_card.lua items.json 42 card-details.json box.jpg > product-card.html
   ```

   Omit the optional photo for a text-only card. Use `-` in place of the
   details file to add a photo without extra details. Choose a local PNG or
   JPEG up to 2 MiB and 12 megapixels (up to 4096 px on each edge). The export
   embeds only the photo explicitly passed on the command line. It cannot
   retrieve an image URL or read a path specified by the details file.
   Open the HTML in a browser and select **Print** or **Save as PDF**.
   Print the preview before making multiple copies.

Fast add/remove requires actual signed warehouse movements, not an edit to the
display quantity. Use normal Ledger documents for existing accounting workflows.
The [UNF stock preview](unf-odata.md) can be used as a separately verified source
for an annotated quantity, but this command does not update Ledger or UNF, nor
does it aggregate by variant or warehouse. The exported HTML may contain internal
item information and embeds the supplied photo; choose what to share.

## Test

```sh
lua apps/ledger/tests/product_card_test.lua
```
