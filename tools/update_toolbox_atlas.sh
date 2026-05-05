#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/orion-toolbox-icons.XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT

LED_URL="https://www.icons101.com/iconset_download/setid_1237/Led_by_Led24/ledicons.zip"
ICON_DIR="$TMPDIR/led"
MANIFEST="$TMPDIR/toolbox_icons.led.txt"
HEADER_TMP="$TMPDIR/toolbox_icons.h"
LICENSE_OUT="$ROOT/examples/formeditor/share/toolbox_LED24_LICENSE.txt"

mkdir -p "$ROOT/examples/formeditor/share" "$ROOT/user" "$ICON_DIR"

curl -fsSL "$LED_URL" -o "$TMPDIR/ledicons.zip"
unzip -q "$TMPDIR/ledicons.zip" -d "$ICON_DIR"

sanitize_icon_name() {
  local name="$1"
  name="${name%.png}"
  name="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]' | tr -c '[:alnum:]_' '_')"
  case "$name" in
    [0-9]*) name="icon_$name" ;;
  esac
  printf '%s\n' "$name"
}

: > "$MANIFEST"
while IFS= read -r icon_path; do
  enum_name="$(sanitize_icon_name "$(basename "$icon_path")")"
  printf '%s %s\n' "$enum_name" "$enum_name" >> "$MANIFEST"
done < <(unzip -Z1 "$TMPDIR/ledicons.zip" | grep '\.png$')

make -C "$ROOT" build/bin/gen_toolbox_atlas
"$ROOT/build/bin/gen_toolbox_atlas" \
  "$MANIFEST" \
  "$ICON_DIR" \
  "$ROOT/examples/formeditor/share/toolbox.png" \
  "$HEADER_TMP"

cp "$HEADER_TMP" "$ROOT/user/toolbox_icons.h"

unzip -p "$TMPDIR/ledicons.zip" 1license.txt > "$LICENSE_OUT"

echo "Updated examples/formeditor/share/toolbox.png and user/toolbox_icons.h"
