#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/tools/toolbox_icons.txt"
TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/orion-toolbox-icons.XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT

BASE_URL="https://unpkg.com/@primer/octicons@19.25.0/build/svg"
DEFAULT_ICON="apps"

octicon_name_for() {
  case "$1" in
    select) printf '%s\n' "crosshairs" ;;
    component) printf '%s\n' "apps" ;;
    box) printf '%s\n' "package" ;;
    textbox) printf '%s\n' "typography" ;;
    button) printf '%s\n' "square" ;;
    radio) printf '%s\n' "circle" ;;
    combobox) printf '%s\n' "single-select" ;;
    listbox) printf '%s\n' "list-unordered" ;;
    treeview) printf '%s\n' "rows" ;;
    slider) printf '%s\n' "sliders" ;;
    scrollbar) printf '%s\n' "grabber" ;;
    progress) printf '%s\n' "meter" ;;
    tabs) printf '%s\n' "tab" ;;
    groupbox) printf '%s\n' "square" ;;
    panel) printf '%s\n' "sidebar-expand" ;;
    spacer) printf '%s\n' "space" ;;
    image) printf '%s\n' "image" ;;
    canvas) printf '%s\n' "image" ;;
    histogram) printf '%s\n' "graph-bar-vertical" ;;
    gradient) printf '%s\n' "paintbrush" ;;
    color_picker) printf '%s\n' "paintbrush" ;;
    preview) printf '%s\n' "eye" ;;
    menu) printf '%s\n' "three-bars" ;;
    menu_item) printf '%s\n' "dot" ;;
    context_menu) printf '%s\n' "kebab-horizontal" ;;
    statusbar) printf '%s\n' "info" ;;
    menubar) printf '%s\n' "three-bars" ;;
    dialog) printf '%s\n' "browser" ;;
    window) printf '%s\n' "browser" ;;
    folder) printf '%s\n' "file-directory" ;;
    open) printf '%s\n' "file-directory-open-fill" ;;
    save) printf '%s\n' "desktop-download" ;;
    print) printf '%s\n' "file" ;;
    settings) printf '%s\n' "gear" ;;
    properties) printf '%s\n' "list-unordered" ;;
    help) printf '%s\n' "question" ;;
    warning) printf '%s\n' "alert" ;;
    error) printf '%s\n' "x-circle" ;;
    info) printf '%s\n' "info" ;;
    success) printf '%s\n' "check-circle" ;;
    cancel) printf '%s\n' "x-circle" ;;
    paste) printf '%s\n' "paste" ;;
    cut) printf '%s\n' "diff-removed" ;;
    undo) printf '%s\n' "undo" ;;
    redo) printf '%s\n' "redo" ;;
    refresh|reload|sync) printf '%s\n' "sync" ;;
    import) printf '%s\n' "repo-pull" ;;
    export) printf '%s\n' "repo-push" ;;
    query) printf '%s\n' "database" ;;
    field) printf '%s\n' "search" ;;
    relation) printf '%s\n' "cross-reference" ;;
    index) printf '%s\n' "list-ordered" ;;
    schema|workflow) printf '%s\n' "flowchart" ;;
    api) printf '%s\n' "code-square" ;;
    connection|network) printf '%s\n' "broadcast" ;;
    socket) printf '%s\n' "plug" ;;
    globe) printf '%s\n' "globe" ;;
    unlock) printf '%s\n' "unlock" ;;
    user) printf '%s\n' "person" ;;
    users) printf '%s\n' "people" ;;
    role) printf '%s\n' "id-badge" ;;
    timer) printf '%s\n' "clock" ;;
    schedule|calendar_event) printf '%s\n' "calendar" ;;
    job|play) printf '%s\n' "play" ;;
    queue) printf '%s\n' "tasklist" ;;
    message|chat) printf '%s\n' "comment" ;;
    phone) printf '%s\n' "device-mobile" ;;
    map) printf '%s\n' "globe" ;;
    pin|address) printf '%s\n' "pin" ;;
    building|company) printf '%s\n' "organization" ;;
    flag) printf '%s\n' "milestone" ;;
    camera) printf '%s\n' "device-camera" ;;
    music|speaker) printf '%s\n' "unmute" ;;
    microphone) printf '%s\n' "mute" ;;
    file_image) printf '%s\n' "file-media" ;;
    file_text) printf '%s\n' "file" ;;
    file_data) printf '%s\n' "database" ;;
    chart|line_chart|pie_chart|area_chart) printf '%s\n' "graph" ;;
    activity) printf '%s\n' "pulse" ;;
    trend_up) printf '%s\n' "arrow-up-right" ;;
    trend_down) printf '%s\n' "arrow-down-right" ;;
    calculator) printf '%s\n' "number" ;;
    script) printf '%s\n' "code" ;;
    test) printf '%s\n' "beaker" ;;
    build) printf '%s\n' "tools" ;;
    repository) printf '%s\n' "repo" ;;
    module|extension) printf '%s\n' "container" ;;
    layer) printf '%s\n' "stack" ;;
    mask) printf '%s\n' "eye-closed" ;;
    transparency) printf '%s\n' "square-circle" ;;
    brush|fill|eyedropper) printf '%s\n' "paintbrush" ;;
    eraser) printf '%s\n' "blocked" ;;
    spray) printf '%s\n' "sparkles-fill" ;;
    wand) printf '%s\n' "sparkle-fill" ;;
    crop) printf '%s\n' "screen-normal" ;;
    move) printf '%s\n' "arrow-both" ;;
    resize|expand) printf '%s\n' "screen-full" ;;
    pan) printf '%s\n' "grabber" ;;
    text) printf '%s\n' "typography" ;;
    shape) printf '%s\n' "diamond" ;;
    line) printf '%s\n' "dash" ;;
    rectangle|rounded_rect) printf '%s\n' "square" ;;
    ellipse) printf '%s\n' "circle" ;;
    polygon) printf '%s\n' "diamond" ;;
    align_left) printf '%s\n' "fold" ;;
    align_center|align_middle) printf '%s\n' "focus-center" ;;
    align_right) printf '%s\n' "unfold" ;;
    align_top) printf '%s\n' "move-to-top" ;;
    align_bottom) printf '%s\n' "move-to-bottom" ;;
    distribute_horizontal) printf '%s\n' "columns" ;;
    distribute_vertical) printf '%s\n' "rows" ;;
    grid) printf '%s\n' "table" ;;
    snap) printf '%s\n' "crosshairs" ;;
    ruler|measure) printf '%s\n' "meter" ;;
    anchor) printf '%s\n' "pin" ;;
    palette|swatch) printf '%s\n' "paintbrush" ;;
    contrast|invert) printf '%s\n' "circle-slash" ;;
    brightness) printf '%s\n' "sun" ;;
    levels) printf '%s\n' "sliders" ;;
    blur) printf '%s\n' "pulse" ;;
    sharpen) printf '%s\n' "zap" ;;
    threshold) printf '%s\n' "horizontal-rule" ;;
    filter_photo) printf '%s\n' "filter" ;;
    file_new) printf '%s\n' "file-added" ;;
    file_open) printf '%s\n' "file-directory-open-fill" ;;
    file_save) printf '%s\n' "desktop-download" ;;
    file_close) printf '%s\n' "file-removed" ;;
    window_tools) printf '%s\n' "tools" ;;
    window_layers) printf '%s\n' "stack" ;;
    window_colors) printf '%s\n' "paintbrush" ;;
    window_forms) printf '%s\n' "checklist" ;;
    window_plugins) printf '%s\n' "plug" ;;
    window_properties) printf '%s\n' "list-unordered" ;;
    window_output|window_console) printf '%s\n' "terminal" ;;
    window_browser) printf '%s\n' "browser" ;;
    window_preview) printf '%s\n' "eye" ;;
    app) printf '%s\n' "apps" ;;
    tablet) printf '%s\n' "device-mobile" ;;
    keyboard) printf '%s\n' "command-palette" ;;
    mouse) printf '%s\n' "grabber" ;;
    printer) printf '%s\n' "file" ;;
    scanner) printf '%s\n' "codescan" ;;
    barcode|qr) printf '%s\n' "hash" ;;
    cart|money|invoice) printf '%s\n' "credit-card" ;;
    dashboard) printf '%s\n' "meter" ;;
    kanban) printf '%s\n' "project" ;;
    timeline) printf '%s\n' "iterations" ;;
    contact) printf '%s\n' "person" ;;
    project) printf '%s\n' "project" ;;
    priority) printf '%s\n' "flame" ;;
    notification) printf '%s\n' "bell" ;;
    subscription) printf '%s\n' "loop" ;;
    backup) printf '%s\n' "cloud" ;;
    restore) printf '%s\n' "history" ;;
    power) printf '%s\n' "circle-slash" ;;
    pause) printf '%s\n' "pause" ;;
    stop) printf '%s\n' "stop" ;;
    record) printf '%s\n' "dot-fill" ;;
    previous) printf '%s\n' "move-to-start" ;;
    next) printf '%s\n' "move-to-end" ;;
    fast_forward) printf '%s\n' "skip" ;;
    rewind) printf '%s\n' "skip" ;;
    collapse) printf '%s\n' "screen-normal" ;;
    fullscreen) printf '%s\n' "screen-full" ;;
    caret_up) printf '%s\n' "chevron-up" ;;
    *) printf '%s\n' "$2" ;;
  esac
}

mkdir -p "$ROOT/examples/formeditor/share" "$ROOT/user"

while read -r enum_name icon_name _; do
  case "${enum_name:-}" in
    ""|\#*) continue ;;
  esac
  manifest_icon_name="$icon_name"
  octicon_name="$(octicon_name_for "$enum_name" "$icon_name")"
  out="$TMPDIR/$manifest_icon_name.svg"
  if [ ! -f "$out" ]; then
    if ! curl -fsSL "$BASE_URL/$octicon_name-16.svg" -o "$out" 2>/dev/null; then
      printf 'Missing Octicon %s; using %s\n' "$octicon_name" "$DEFAULT_ICON" >&2
      curl -fsSL "$BASE_URL/$DEFAULT_ICON-16.svg" -o "$out"
    fi
  fi
done < "$MANIFEST"

make -C "$ROOT" build/bin/gen_toolbox_atlas
"$ROOT/build/bin/gen_toolbox_atlas" \
  "$MANIFEST" \
  "$TMPDIR" \
  "$ROOT/examples/formeditor/share/toolbox.png" \
  "$ROOT/user/toolbox_icons.h"

curl -fsSL "https://unpkg.com/@primer/octicons@19.25.0/LICENSE" \
  -o "$ROOT/examples/formeditor/share/toolbox_OCTICONS_LICENSE.txt"

echo "Updated examples/formeditor/share/toolbox.png and user/toolbox_icons.h"
