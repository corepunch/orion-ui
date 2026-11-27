#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
lua=${1:-lua5.4}
tool=apps/ledger/tools/workshop.lua
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT HUP INT TERM
"$lua" "$tool" init > "$dir/empty.json"
cat > "$dir/register.json" <<'JSON'
{"request_id":"initial-1","expected_revision":0,"actor":"operator-1","at_min":100000,
 "command":"vehicle.register","args":{"id":"car-1","owner_ref":"customer-1","vin":"WVWZZZ1JZ3W386752",
 "plate":"B XR 100","jurisdiction":"DE-BE","make":"Volkswagen","model":"Golf","year":2003}}
JSON
"$lua" "$tool" apply "$dir/empty.json" "$dir/register.json" > "$dir/workshop.json"
"$lua" "$tool" apply "$dir/workshop.json" "$dir/register.json" > "$dir/replayed.json"
cmp "$dir/workshop.json" "$dir/replayed.json"
printf '[]\n' | cmp "$dir/empty.json" -
printf '{"plate":"B-XR100","jurisdiction":"de-be"}\n' > "$dir/query.json"
"$lua" "$tool" find "$dir/workshop.json" "$dir/query.json" | grep -q '"id":"car-1"'
"$lua" "$tool" history "$dir/workshop.json" car-1 | grep -Fq '"orders":[]'
sed 's/initial-1/initial-2/g' "$dir/register.json" > "$dir/stale.json"
if "$lua" "$tool" apply "$dir/workshop.json" "$dir/stale.json" > "$dir/denied.json" 2> "$dir/error"; then
  echo "stale revision was accepted" >&2
  exit 1
fi
test ! -s "$dir/denied.json"
grep -q 'stale revision' "$dir/error"
printf '%s\n' '{"request_id":"order-1","expected_revision":1,"actor":"operator-1","at_min":100000,"command":"order.open","args":{"id":"order-1","vehicle":"car-1","complaint":"<test> & brake noise"}}' > "$dir/order.json"
"$lua" "$tool" apply "$dir/workshop.json" "$dir/order.json" > "$dir/open.json"
"$lua" "$tool" print "$dir/open.json" order-1 | grep -Fq '&lt;test&gt; &amp; brake noise'
printf '%s\n' '{"request_id":"bay-1","expected_revision":2,"actor":"operator-1","at_min":100000,"command":"resource.register","args":{"id":"bay-1","name":"Lift","kind":"bay"}}' > "$dir/bay.json"
"$lua" "$tool" apply "$dir/open.json" "$dir/bay.json" > "$dir/resources.json"
"$lua" "$tool" report-csv "$dir/resources.json" 100000 100060 | grep -Fxq 'bay-1,100000,100060,0,0,'
printf '%s\n' "Workshop CLI passed"
