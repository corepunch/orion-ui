# Print item labels

From Ledger's **Items** page, select a catalog row and click **Labels**, or
use **Catalogs → Print Item Labels**. The item is preselected; select another
from the dropdown if needed. A label prints the item name, article/SKU and
a Code 128 barcode. The barcode is the literal SKU by default. To encode a
different existing internal identifier, enter an optional override.
Leading zeroes are preserved.

- **One kind of label:** set Copies and choose **Save Printable Labels**.
- **Batch:** choose each item and quantity and use **Add to Batch**, then
  **Save Printable Labels**. Use **Clear Batch** to start over. A batch is a
  temporary print queue; nothing changes in accounting or the item catalog.
- **Paper:** A4 is the default. Optional US Letter uses a different layout.
  Both are 3×8 cut sheets, not universal pre-cut stationery templates.
  **Label roll** produces one page per label using the specified width and
  height in millimeters (38–120 × 28–100 mm). The document has no page margins;
  the barcode has the clear space required for scanning.
- Open the saved HTML locally in a browser and print or save as PDF. Use
  **Actual size / 100%**, disable browser headers/footers, and use no additional
  margins. Select the matching paper size in the printer driver. First verify
  dimensions on plain paper and scan a sample; the driver may impose its own
  device margins or scale.

This is for internal inventory labels, not GTIN/EAN allocation, GS1-128,
carrier or «Честный знак» labels. Codes must be printable ASCII; the display
name can use Unicode. Labels that would force a module narrower than 0.25 mm
are refused. Select wider paper or a shorter code instead of shrinking the
print. Maximum 500 labels per job. No scanner or printer device was tested.

## Headless input

```json
{
  "options":{"format":"single","stock_width_mm":62,"stock_height_mm":32},
  "rows":[
    {"name":"Nut","sku":"000123","copies":2},
    {"name":"Резистор","sku":"R-8","barcode":"MIX-A9","copies":1}
  ]
}
```

```sh
lua5.4 apps/ledger/tools/label_sheet.lua labels.json > labels.html
lua5.4 apps/ledger/tests/label_sheet_test.lua
```

Use the saved file only if the command succeeds. An independent PDF check
rasterized two 62 × 32 mm pages at 300 dpi, and ZXing decoded the exact payloads
`000123` and `MIX-A9`. This tests the printed representation in software,
not a physical printer, scanner or driver.
