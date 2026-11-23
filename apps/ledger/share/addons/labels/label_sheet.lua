-- Renderer shared by the Ledger add-on and headless label workflows.
-- Code 128 symbol widths indexed by codeword + 1 (ISO/IEC 15417).
local M = {}
local patterns = {}
for widths in ([[
212222 222122 222221 121223 121322 131222 122213 122312 132212 221213 221312 231212
112232 122132 122231 113222 123122 123221 223211 221132 221231 213212 223112 312131
311222 321122 321221 312212 322112 322211 212123 212321 232121 111323 131123 131321
112313 132113 132311 211313 231113 231311 112133 112331 132131 113123 113321 133121
313121 211331 231131 213113 213311 213131 311123 311321 331121 312113 312311 332111
314111 221411 431111 111224 111422 121124 121421 141122 141221 112214 112412 122114
122411 142112 142211 241211 221114 413111 241112 134111 111242 121142 121241 114212
124112 124211 411212 421112 421211 212141 214121 412121 111143 111341 131141 114113
114311 411113 411311 113141 114131 311141 411131 211412 211214 211232 2331112
]]):gmatch("%d+") do patterns[#patterns + 1] = widths end
assert(#patterns == 107)

local function text(v, limit, what)
  assert(type(v) == "string" and #v <= limit and utf8.len(v) and v:match("%S") and not v:find("%c"),
    "Invalid " .. what .. ".")
  return v
end

local function html(v)
  return (v:gsub("[&<>\"']", {
    ["&"]="&amp;", ["<"]="&lt;", [">"]="&gt;", ['"']="&quot;", ["'"]="&#39;",
  }))
end

function M.encode(value)
  assert(type(value) == "string" and #value >= 1 and #value <= 64
    and not value:find("[^\32-\126]"), "Barcode must contain 1–64 printable ASCII characters.")
  -- Code C halves the width of an even-length number without discarding zeros.
  local numeric = #value % 2 == 0 and value:match("^%d+$")
  local codes = {numeric and 105 or 104}
  if numeric then
    for i = 1, #value, 2 do codes[#codes + 1] = tonumber(value:sub(i, i + 1)) end
  else
    for i = 1, #value do codes[#codes + 1] = value:byte(i) - 32 end
  end
  local check = codes[1]
  for i = 2, #codes do check = check + (i - 1) * codes[i] end
  codes[#codes + 1] = check % 103
  codes[#codes + 1] = 106
  return codes
end

local function nstr(v)
  return (("%.3f"):format(v):gsub(",", "."))
end

function M.symbol(value, width)
  local codes, bars, x = M.encode(value), {}, 10
  for _, code in ipairs(codes) do
    for index = 1, #patterns[code + 1] do
      local run = tonumber(patterns[code + 1]:sub(index, index))
      if index % 2 == 1 then
        bars[#bars + 1] = ('<rect x="%d" y="0" width="%d" height="1"/>'):format(x, run)
      end
      x = x + run
    end
  end
  x = x + 10  -- ten modules of clear space on both sides, including stop.
  local mm = math.min(width, x * 0.5)
  assert(mm / x >= 0.25, "Barcode is too wide for this stock. Use a wider label or shorter code.")
  return ('<svg class="barcode" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="%s"'
    .. ' width="%smm" height="10mm" viewBox="0 0 %d 1" preserveAspectRatio="none" shape-rendering="crispEdges">'
    .. '<rect width="%d" height="1" fill="white"/><g fill="black">%s</g></svg>')
    :format(html(value), nstr(mm), x, x, table.concat(bars))
end

local paper = {
  a4 = {width=210, height=297, columns=3, rows=8, label_width=64, label_height=33, gap=2, left=7, top=9.5},
  letter = {width=215.9, height=279.4, columns=3, rows=8, label_width=64, label_height=31, gap=2, left=9.95, top=8.7},
}

function M.layout(options)
  options = options or {}
  local name = options.format or "a4"
  if paper[name] then return paper[name] end
  assert(name == "single", "Choose A4, Letter, or a single label roll.")
  local w, h = options.stock_width_mm, options.stock_height_mm
  assert(type(w) == "number" and w >= 38 and w <= 120 and w < math.huge
    and type(h) == "number" and h >= 28 and h <= 100 and h < math.huge,
      "Roll size must be 38–120 mm wide and 28–100 mm tall.")
  return {width=w, height=h, columns=1, rows=1, label_width=w, label_height=h, gap=0, left=0, top=0}
end

function M.render(rows, options)
  local layout = M.layout(options)
  assert(type(rows) == "table" and #rows > 0 and #rows <= 500, "Add between 1 and 500 label lines.")
  local labels, inputs = {}, 0
  for k in pairs(rows) do
    assert(math.type(k) == "integer" and k >= 1 and k <= #rows, "Label lines must be an array.")
    inputs = inputs + 1
  end
  assert(inputs == #rows, "Label lines must be an array.")
  -- Resolve and validate the complete queue before returning any printable output.
  for _, row in ipairs(rows) do
    assert(type(row) == "table", "Invalid label line.")
    local sku = text(row.sku, 64, "SKU")
    local name = text(row.name, 240, "item name")
    local barcode = row.barcode or sku
    local qty = row.copies
    assert(type(qty) == "number" and math.type(qty) == "integer" and qty >= 1 and qty <= 500
      and #labels + qty <= 500, "Use whole copy counts and at most 500 labels in total.")
    local symbol = M.symbol(barcode, layout.label_width - 4)
    local label = '<section class="label"><div class="item">' .. html(name) .. '</div><div class="sku">'
      .. html(sku) .. '</div>' .. symbol .. '<div class="encoded">' .. html(barcode) .. '</div></section>'
    for _ = 1, qty do labels[#labels + 1] = label end
  end
  local doc = {'<!doctype html><html lang="en"><head><meta charset="utf-8">',
    '<meta http-equiv="Content-Security-Policy" content="default-src \'none\';style-src \'unsafe-inline\'">',
    '<title>Item labels</title><style>',
    '@page{size:', nstr(layout.width), 'mm ', nstr(layout.height), 'mm;margin:0}',
    '*{box-sizing:border-box}html,body{margin:0;padding:0;background:#fff;color:#000}',
    '.page{width:', nstr(layout.width), 'mm;padding:', nstr(layout.top), 'mm 0 0 ', nstr(layout.left),
    'mm;display:grid;grid-template-columns:repeat(', layout.columns, ',', nstr(layout.label_width),
    'mm);grid-auto-rows:', nstr(layout.label_height), 'mm;gap:', nstr(layout.gap), 'mm}',
    '.page+.page{break-before:page}',
    '.label{display:flex;flex-direction:column;align-items:center;justify-content:center;overflow:hidden;',
    'padding:1mm 2mm;text-align:center;font:9pt/1.05 system-ui,sans-serif}',
    '.item{font-weight:bold;line-height:1.05;max-height:2.1em;overflow:hidden;overflow-wrap:anywhere}',
    '.sku,.encoded{max-width:100%;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}',
    '.sku{font-size:8pt}.encoded{font:7.5pt/1 monospace}.barcode{display:block;flex:none;margin:.6mm 0}',
    '@media screen{body{background:#eee}.page{margin:10px auto;background:#fff}.label{outline:1px dotted #ccc}}',
    '</style></head><body>'}
  local per_page = layout.columns * layout.rows
  for i, label in ipairs(labels) do
    if (i - 1) % per_page == 0 then doc[#doc + 1] = '<div class="page">' end
    doc[#doc + 1] = label
    if i % per_page == 0 or i == #labels then doc[#doc + 1] = '</div>' end
  end
  doc[#doc + 1] = '</body></html>'
  return table.concat(doc), #labels, (#labels + per_page - 1) // per_page
end

return M
