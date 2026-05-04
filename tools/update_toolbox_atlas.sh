#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/tools/toolbox_icons.txt"
TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/orion-toolbox-icons.XXXXXX")"
trap 'rm -rf "$TMPDIR"' EXIT
RESOLVED_MANIFEST="$TMPDIR/toolbox_icons.resolved.txt"

FUGUE_URL="https://p.yusukekamiyamane.com/icon/downloads/fugue-icons-3.5.6.zip"
DEFAULT_ICON="application"

fugue_candidates_for() {
  case "$1" in
    select) printf '%s\n' "cursor control-cursor mouse-select" ;;
    component) printf '%s\n' "applications application" ;;
    box) printf '%s\n' "box" ;;
    label) printf '%s\n' "ui-label tag-label" ;;
    textbox) printf '%s\n' "ui-text-field ui-text-area" ;;
    button) printf '%s\n' "ui-button ui-button-default" ;;
    checkbox) printf '%s\n' "ui-check-box ui-check-box-uncheck" ;;
    radio) printf '%s\n' "ui-radio-button ui-radio-button-uncheck" ;;
    combobox) printf '%s\n' "ui-combo-box ui-text-field-select" ;;
    listbox) printf '%s\n' "ui-list-box application-list" ;;
    reportview|table|grid) printf '%s\n' "ui-scroll-pane-table application-table table" ;;
    treeview) printf '%s\n' "ui-scroll-pane-tree application-tree" ;;
    slider) printf '%s\n' "ui-slider ui-seek-bar" ;;
    scrollbar) printf '%s\n' "ui-scroll-bar ui-scroll-pane" ;;
    progress) printf '%s\n' "ui-progress-bar" ;;
    tabs) printf '%s\n' "ui-tab ui-tab-content" ;;
    groupbox) printf '%s\n' "ui-group-box" ;;
    panel) printf '%s\n' "ui-panel ui-layout-panel" ;;
    spacer) printf '%s\n' "ui-spacer" ;;
    image|canvas) printf '%s\n' "image application-image" ;;
    histogram) printf '%s\n' "chart data-histogram chart-up" ;;
    gradient) printf '%s\n' "color-swatch color-adjustment" ;;
    color_picker) printf '%s\n' "ui-color-picker color-swatch palette" ;;
    preview) printf '%s\n' "eye application-search-result" ;;
    menu|menubar) printf '%s\n' "ui-menu" ;;
    menu_item) printf '%s\n' "bullet-small dot" ;;
    context_menu) printf '%s\n' "ui-menu-blue ui-menu" ;;
    toolbar|window_tools|build) printf '%s\n' "ui-toolbar wrench-screwdriver wrench" ;;
    statusbar) printf '%s\n' "ui-status-bar" ;;
    dialog) printf '%s\n' "application-dialog application-form" ;;
    window) printf '%s\n' "application application-browser" ;;
    window_browser) printf '%s\n' "globe-model application-browser globe" ;;
    app) printf '%s\n' "applications-stack applications-blue application" ;;
    file) printf '%s\n' "document blue-document" ;;
    folder) printf '%s\n' "folder blue-folder" ;;
    open|file_open) printf '%s\n' "folder-open blue-folder-open" ;;
    save|file_save) printf '%s\n' "disk disk-black" ;;
    print) printf '%s\n' "printer" ;;
    printer) printf '%s\n' "printer-color printer-medium printer" ;;
    search) printf '%s\n' "ui-search-field magnifier document-search-result" ;;
    filter) printf '%s\n' "funnel funnel-small" ;;
    settings) printf '%s\n' "gear gear-small" ;;
    properties) printf '%s\n' "property property-blue server-property" ;;
    window_properties) printf '%s\n' "property-blue property gear" ;;
    help) printf '%s\n' "question-button question" ;;
    warning) printf '%s\n' "exclamation-button exclamation" ;;
    error) printf '%s\n' "cross-button cross" ;;
    info) printf '%s\n' "information-button information" ;;
    success) printf '%s\n' "tick-button tick" ;;
    cancel) printf '%s\n' "cross-button cross" ;;
    close) printf '%s\n' "cross-small cross" ;;
    file_close) printf '%s\n' "blue-document-hf-delete document--minus blue-document--minus" ;;
    add|file_new) printf '%s\n' "plus plus-button document--plus" ;;
    delete) printf '%s\n' "cross document--minus bin" ;;
    edit) printf '%s\n' "edit pencil--pencil pencil" ;;
    copy) printf '%s\n' "document-copy clipboard" ;;
    paste) printf '%s\n' "clipboard-paste clipboard" ;;
    cut) printf '%s\n' "scissors" ;;
    undo) printf '%s\n' "arrow-curve-180-left arrow-return-180-left" ;;
    redo) printf '%s\n' "arrow-curve arrow-return" ;;
    refresh) printf '%s\n' "arrow-repeat" ;;
    reload) printf '%s\n' "arrow-circle-double arrow-repeat-once" ;;
    sync) printf '%s\n' "arrow-retweet arrow-switch" ;;
    upload|backup) printf '%s\n' "drive-upload arrow-090" ;;
    download|restore) printf '%s\n' "drive-download arrow-270" ;;
    import) printf '%s\n' "blue-document-import application-import" ;;
    export) printf '%s\n' "blue-document-export application-export" ;;
    database) printf '%s\n' "database databases database-medium" ;;
    query) printf '%s\n' "database-sql sql sql-join" ;;
    file_data) printf '%s\n' "blue-document-table database-property database-small" ;;
    field) printf '%s\n' "pencil-field ui-text-field-small ui-search-field" ;;
    relation) printf '%s\n' "databases-relation chain" ;;
    index) printf '%s\n' "edit-list-order blue-document-number" ;;
    schema) printf '%s\n' "sitemap database-property node-design" ;;
    workflow) printf '%s\n' "node-design arrow-branch arrow-join" ;;
    server) printf '%s\n' "server servers server-medium" ;;
    cloud) printf '%s\n' "cloud network-cloud database-cloud" ;;
    api) printf '%s\n' "application-network document-code" ;;
    code) printf '%s\n' "document-code blue-document-code" ;;
    script) printf '%s\n' "blue-document-code document-code" ;;
    webhook) printf '%s\n' "chain--arrow chain" ;;
    link) printf '%s\n' "chain" ;;
    unlink) printf '%s\n' "chain-unchain" ;;
    plug) printf '%s\n' "plug-connect plug" ;;
    plugin) printf '%s\n' "puzzle--plus toolbox plug--plus" ;;
    window_plugins) printf '%s\n' "plug--plus toolbox plug" ;;
    connection) printf '%s\n' "network-ethernet chain--arrow plug-connect" ;;
    network) printf '%s\n' "network network-hub network-wireless" ;;
    socket) printf '%s\n' "plug" ;;
    globe|map) printf '%s\n' "globe globe-network" ;;
    lock) printf '%s\n' "lock" ;;
    unlock) printf '%s\n' "lock-unlock" ;;
    user|contact) printf '%s\n' "user" ;;
    users) printf '%s\n' "users" ;;
    role) printf '%s\n' "user-business" ;;
    shield) printf '%s\n' "shield" ;;
    calendar) printf '%s\n' "calendar" ;;
    calendar_event) printf '%s\n' "calendar-day calendar-list" ;;
    schedule) printf '%s\n' "calendar-task calendar-clock" ;;
    clock) printf '%s\n' "clock" ;;
    timer) printf '%s\n' "clock-select clock-frame" ;;
    stopwatch) printf '%s\n' "clock-history" ;;
    job) printf '%s\n' "control" ;;
    play) printf '%s\n' "control-000-small control" ;;
    queue|task) printf '%s\n' "clipboard-task application-task" ;;
    event) printf '%s\n' "bell" ;;
    notification) printf '%s\n' "bell-small bell--plus bell" ;;
    mail) printf '%s\n' "mail" ;;
    message) printf '%s\n' "balloon" ;;
    chat) printf '%s\n' "balloons balloon-ellipsis" ;;
    phone) printf '%s\n' "telephone mobile-phone" ;;
    mobile) printf '%s\n' "mobile-phone-medium mobile-phone" ;;
    tablet) printf '%s\n' "media-player-phone-horizontal media-player-phone" ;;
    pin) printf '%s\n' "pin" ;;
    location) printf '%s\n' "geolocation map-pin" ;;
    address) printf '%s\n' "address-book card-address" ;;
    anchor) printf '%s\n' "pin-small pin--arrow" ;;
    home) printf '%s\n' "home" ;;
    building) printf '%s\n' "building" ;;
    company) printf '%s\n' "building-medium building-low building" ;;
    package) printf '%s\n' "box-label box-small" ;;
    archive) printf '%s\n' "box-zipper inbox" ;;
    tag) printf '%s\n' "tag" ;;
    bookmark) printf '%s\n' "bookmark" ;;
    star) printf '%s\n' "star" ;;
    heart) printf '%s\n' "heart" ;;
    flag) printf '%s\n' "flag" ;;
    priority) printf '%s\n' "flag-pink exclamation-red" ;;
    milestone) printf '%s\n' "milestone-calendar flag-blue" ;;
    camera|scanner) printf '%s\n' "camera scanner" ;;
    video) printf '%s\n' "camcorder" ;;
    music|speaker) printf '%s\n' "speaker speaker-volume" ;;
    microphone) printf '%s\n' "microphone" ;;
    file_image) printf '%s\n' "blue-document-image document-image" ;;
    file_text) printf '%s\n' "blue-document-text document-text" ;;
    file_code) printf '%s\n' "blue-document-code document-code" ;;
    chart) printf '%s\n' "chart-medium chart" ;;
    line_chart) printf '%s\n' "chart-up-color chart-up" ;;
    area_chart) printf '%s\n' "chart-medium chart--arrow" ;;
    pie_chart) printf '%s\n' "chart-pie" ;;
    activity) printf '%s\n' "radioactivity chart-up-color" ;;
    trend_up) printf '%s\n' "chart-up arrow-045" ;;
    trend_down) printf '%s\n' "chart-down arrow-135" ;;
    calculator) printf '%s\n' "calculator" ;;
    terminal) printf '%s\n' "application-terminal terminal" ;;
    window_output) printf '%s\n' "terminal terminal-medium application-terminal" ;;
    window_console) printf '%s\n' "terminal-network terminal-protector terminal" ;;
    bug) printf '%s\n' "bug" ;;
    test) printf '%s\n' "beaker" ;;
    deploy) printf '%s\n' "rocket" ;;
    branch) printf '%s\n' "arrow-branch" ;;
    commit) printf '%s\n' "node" ;;
    merge) printf '%s\n' "arrow-merge" ;;
    repository) printf '%s\n' "blue-folder-tree folder-tree" ;;
    module) printf '%s\n' "box-document box application-block" ;;
    extension) printf '%s\n' "puzzle puzzle--plus" ;;
    layer|window_layers) printf '%s\n' "layer layers" ;;
    mask) printf '%s\n' "layer-mask eye-close" ;;
    transparency) printf '%s\n' "layer-transparent checkerboard" ;;
    brush) printf '%s\n' "paint-brush" ;;
    pencil) printf '%s\n' "pencil pencil-small pencil-color" ;;
    eraser) printf '%s\n' "eraser" ;;
    fill) printf '%s\n' "paint-can paint-can-color" ;;
    spray) printf '%s\n' "paint-brush-color" ;;
    wand) printf '%s\n' "wand-magic wand" ;;
    crop) printf '%s\n' "ruler-crop" ;;
    move) printf '%s\n' "arrow-move" ;;
    resize|expand) printf '%s\n' "application-resize-full arrow-resize" ;;
    fullscreen) printf '%s\n' "monitor-window application-resize-full arrow-resize" ;;
    zoom_in) printf '%s\n' "magnifier-zoom-in" ;;
    zoom_out) printf '%s\n' "magnifier-zoom-out" ;;
    pan) printf '%s\n' "hand" ;;
    eyedropper) printf '%s\n' "pipette ui-color-picker" ;;
    text) printf '%s\n' "edit-heading application-text" ;;
    shape) printf '%s\n' "layer-shape" ;;
    line) printf '%s\n' "layer-shape-line" ;;
    rectangle) printf '%s\n' "zone-select layer-shape" ;;
    rounded_rect) printf '%s\n' "layer-shape-round" ;;
    ellipse) printf '%s\n' "layer-shape-ellipse" ;;
    polygon) printf '%s\n' "layer-shape-polygon" ;;
    align_left) printf '%s\n' "layers-alignment-left edit-alignment" ;;
    align_center) printf '%s\n' "layers-alignment-center edit-alignment-center" ;;
    align_right) printf '%s\n' "layers-alignment-right edit-alignment-right" ;;
    align_top) printf '%s\n' "layers-alignment-top edit-vertical-alignment-top" ;;
    align_middle) printf '%s\n' "layers-alignment-middle edit-vertical-alignment-middle" ;;
    align_bottom) printf '%s\n' "layers-alignment-bottom" ;;
    distribute_horizontal) printf '%s\n' "layers-arrange" ;;
    distribute_vertical) printf '%s\n' "layers-stack-arrange" ;;
    snap) printf '%s\n' "magnet" ;;
    ruler|measure) printf '%s\n' "ui-ruler ruler" ;;
    palette) printf '%s\n' "palette" ;;
    swatch) printf '%s\n' "color-swatches color-swatch-small" ;;
    window_colors) printf '%s\n' "palette-color color-swatches" ;;
    contrast|invert) printf '%s\n' "contrast contrast-control" ;;
    brightness) printf '%s\n' "brightness brightness-control" ;;
    levels) printf '%s\n' "color-adjustment" ;;
    blur) printf '%s\n' "water" ;;
    sharpen) printf '%s\n' "wand" ;;
    threshold) printf '%s\n' "ui-separator" ;;
    filter_photo) printf '%s\n' "funnel--arrow funnel-small" ;;
    window_forms) printf '%s\n' "application-form" ;;
    window_preview) printf '%s\n' "eye--arrow eye" ;;
    desktop) printf '%s\n' "monitor" ;;
    keyboard) printf '%s\n' "keyboard" ;;
    mouse) printf '%s\n' "mouse" ;;
    barcode|qr) printf '%s\n' "barcode barcode-2d" ;;
    cart) printf '%s\n' "shopping-basket" ;;
    money|credit_card|invoice) printf '%s\n' "credit-card money" ;;
    report) printf '%s\n' "blue-document-table" ;;
    dashboard) printf '%s\n' "application-detail application-monitor" ;;
    kanban) printf '%s\n' "application-tile-horizontal" ;;
    timeline) printf '%s\n' "film-timeline clock-history" ;;
    project) printf '%s\n' "briefcase blueprint" ;;
    subscription) printf '%s\n' "arrow-repeat-once" ;;
    history) printf '%s\n' "magnifier-history clock-history-frame" ;;
    power) printf '%s\n' "control-power" ;;
    pause) printf '%s\n' "control-pause" ;;
    stop) printf '%s\n' "control-stop-square" ;;
    record) printf '%s\n' "control-record" ;;
    previous) printf '%s\n' "control-skip-180" ;;
    next) printf '%s\n' "control-skip" ;;
    fast_forward) printf '%s\n' "control-double" ;;
    rewind) printf '%s\n' "control-double-180" ;;
    collapse) printf '%s\n' "application-resize-actual" ;;
    minimize) printf '%s\n' "minus minus-button" ;;
    maximize) printf '%s\n' "application-resize application-resize-full" ;;
    check) printf '%s\n' "tick" ;;
    arrow_up) printf '%s\n' "arrow-090-small arrow-090" ;;
    arrow_down) printf '%s\n' "arrow-270-small arrow-270" ;;
    arrow_left) printf '%s\n' "arrow-180" ;;
    arrow_right) printf '%s\n' "arrow" ;;
    caret_up) printf '%s\n' "control-090-small arrow-090-small" ;;
    *) printf '%s\n' "$2" ;;
  esac
}

mkdir -p "$ROOT/examples/formeditor/share" "$ROOT/user"
curl -fsSL "$FUGUE_URL" -o "$TMPDIR/fugue-icons.zip"
unzip -q "$TMPDIR/fugue-icons.zip" -d "$TMPDIR/fugue"

find_icon() {
  local name="$1"
  local candidate
  for candidate in \
    "$TMPDIR/fugue/icons-shadowless/$name.png" \
    "$TMPDIR/fugue/icons/$name.png" \
    "$TMPDIR/fugue/bonus/icons-shadowless/$name.png" \
    "$TMPDIR/fugue/bonus/icons/$name.png"
  do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

icon_key() {
  basename "$1" .png
}

USED_ICONS="$TMPDIR/used-icons.txt"
USED_HASHES="$TMPDIR/used-hashes.txt"
: > "$USED_ICONS"
: > "$USED_HASHES"
: > "$RESOLVED_MANIFEST"

is_icon_used() {
  grep -qxF "$1" "$USED_ICONS"
}

mark_icon_used() {
  printf '%s\n' "$1" >> "$USED_ICONS"
}

icon_hash() {
  shasum -a 256 "$1" | awk '{print $1}'
}

is_icon_hash_used() {
  grep -qxF "$1" "$USED_HASHES"
}

mark_icon_file_used() {
  mark_icon_used "$(icon_key "$1")"
  icon_hash "$1" >> "$USED_HASHES"
}

is_icon_file_used() {
  is_icon_used "$(icon_key "$1")" || is_icon_hash_used "$(icon_hash "$1")"
}

find_unused_icon() {
  local dir candidate key
  for dir in \
    "$TMPDIR/fugue/icons-shadowless" \
    "$TMPDIR/fugue/icons" \
    "$TMPDIR/fugue/bonus/icons-shadowless" \
    "$TMPDIR/fugue/bonus/icons"
  do
    [ -d "$dir" ] || continue
    for candidate in "$dir"/*.png; do
      [ -f "$candidate" ] || continue
      key="$(icon_key "$candidate")"
      if ! is_icon_used "$key" && ! is_icon_hash_used "$(icon_hash "$candidate")"; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done
  done
  return 1
}

while read -r enum_name icon_name _; do
  case "${enum_name:-}" in
    ""|\#*) continue ;;
  esac

  out="$TMPDIR/$enum_name.png"
  found=""
  for fugue_name in $(fugue_candidates_for "$enum_name" "$icon_name") "$icon_name" "$DEFAULT_ICON"; do
    if found="$(find_icon "$fugue_name")" && ! is_icon_file_used "$found"; then
      cp "$found" "$out"
      mark_icon_file_used "$found"
      break
    fi
  done
  if [ ! -f "$out" ]; then
    if found="$(find_unused_icon)"; then
      printf 'No unique Fugue candidate for %s; using %s\n' \
             "$enum_name" "$(icon_key "$found")" >&2
      cp "$found" "$out"
      mark_icon_file_used "$found"
    else
      printf 'No unused Fugue icon available for %s\n' "$enum_name" >&2
      exit 1
    fi
  fi
  printf '%s %s\n' "$enum_name" "$enum_name" >> "$RESOLVED_MANIFEST"
done < "$MANIFEST"

make -C "$ROOT" build/bin/gen_toolbox_atlas
"$ROOT/build/bin/gen_toolbox_atlas" \
  "$RESOLVED_MANIFEST" \
  "$TMPDIR" \
  "$ROOT/examples/formeditor/share/toolbox.png" \
  "$ROOT/user/toolbox_icons.h"

unzip -p "$TMPDIR/fugue-icons.zip" README.txt \
  > "$ROOT/examples/formeditor/share/toolbox_FUGUE_README.txt"

echo "Updated examples/formeditor/share/toolbox.png and user/toolbox_icons.h"
