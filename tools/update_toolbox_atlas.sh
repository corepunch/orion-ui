#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/tools/toolbox_icons.txt"
TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/orion-toolbox-icons.XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT

BASE_URL="https://unpkg.com/@tabler/icons-png@3.41.1/icons/outline"
DEFAULT_ICON="box"

mkdir -p "$ROOT/examples/formeditor/share" "$ROOT/user"

while read -r enum_name icon_name _; do
  case "${enum_name:-}" in
    ""|\#*) continue ;;
  esac
  out="$TMPDIR/$icon_name.png"
  if [ ! -f "$out" ]; then
    if ! curl -fsSL "$BASE_URL/$icon_name.png" -o "$out" 2>/dev/null; then
      printf 'Missing Tabler icon %s; using %s\n' "$icon_name" "$DEFAULT_ICON" >&2
      curl -fsSL "$BASE_URL/$DEFAULT_ICON.png" -o "$out"
    fi
  fi
done < "$MANIFEST"

make -C "$ROOT" build/bin/gen_toolbox_atlas
"$ROOT/build/bin/gen_toolbox_atlas" \
  "$MANIFEST" \
  "$TMPDIR" \
  "$ROOT/examples/formeditor/share/toolbox.png" \
  "$ROOT/user/toolbox_icons.h"

curl -fsSL "https://unpkg.com/@tabler/icons-png@3.41.1/LICENSE" \
  -o "$ROOT/examples/formeditor/share/toolbox_TABLER_LICENSE.txt"

echo "Updated examples/formeditor/share/toolbox.png and user/toolbox_icons.h"
