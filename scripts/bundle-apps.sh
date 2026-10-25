#!/bin/sh
# Portable per-app downloads for the platform this runs on, for people who
# want to run one application without the orion package manager.
#
#   sh scripts/bundle-apps.sh [PLATFORM] OUTPUT_DIR [APP...]
#
# PLATFORM is linux-x86_64, macos-arm64 or windows-x86_64 ("" detects it).
# APP defaults to every application in packaging/packages.tsv.
# Each app becomes OUTPUT_DIR/<app>-<platform>.tar.gz (Linux) or .zip (macOS,
# Windows). Names carry no version, so a release asset is always reachable as
# https://github.com/corepunch/orion-ui/releases/latest/download/<app>-<platform>.zip
set -eu

[ $# -ge 2 ] || { echo "usage: $0 [PLATFORM] OUTPUT_DIR [APP...]" >&2; exit 2; }
platform=$1
output=$2
shift 2
version=${ORION_VERSION:-$(cat VERSION 2>/dev/null || echo 0.0.0)}

if [ -z "$platform" ]; then
  case "$(uname -s)" in
    Linux)        platform="linux-$(uname -m)" ;;
    Darwin)       platform="macos-${ARCH:-arm64}" ;;
    MINGW*|MSYS*) platform="windows-x86_64" ;;
    *) echo "bundle-apps: unsupported system $(uname -s)" >&2; exit 2 ;;
  esac
fi
case $platform in
  linux-*)   os=linux;   exe="";     lib_ext=so ;;
  macos-*)   os=macos;   exe="";     lib_ext=dylib ;;
  windows-*) os=windows; exe=".exe"; lib_ext=dll ;;
  *) echo "bundle-apps: unknown platform $platform" >&2; exit 2 ;;
esac

mkdir -p "$output"
output=$(cd "$output" && pwd)
stage=$(mktemp -d "${TMPDIR:-/tmp}/orion-bundle.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
prefix="$stage/root/orion"
make install DESTDIR="$stage/root" PREFIX=/orion >/dev/null

if [ $# -gt 0 ]; then apps="$*"; else apps=$(cut -f1 packaging/packages.tsv); fi
core_libs="libkernel libuser libcommctl libcommdlg libplatform"

sha256_line() {
  if command -v sha256sum >/dev/null 2>&1; then (cd "$output" && sha256sum "$1")
  else (cd "$output" && shasum -a 256 "$1"); fi
}

# bin/, lib/ and share/ mirror the installed prefix: apps find resources in
# <exe_dir>/../share/{orion,<app>} and Linux binaries load lib/ via $ORIGIN.
stage_tree() {
  dir=$1
  mkdir -p "$dir/bin" "$dir/share"
  cp "$prefix/bin/$app$exe" "$dir/bin/"
  cp -R "$prefix/share/orion" "$dir/share/"
  for s in $src $deps; do
    [ -d "$prefix/share/$s" ] && cp -R "$prefix/share/$s" "$dir/share/"
  done
  # Windows looks for DLLs next to the executable.
  if [ $os = windows ]; then libdir="$dir/bin"; else libdir="$dir/lib"; fi
  mkdir -p "$libdir"
  for l in $core_libs; do cp "$prefix/lib/$l.$lib_ext" "$libdir/"; done
  plugin="$prefix/lib/${src}_components.$lib_ext"
  [ -f "$plugin" ] && cp "$plugin" "$libdir/"
  for d in $deps; do
    [ -f "$prefix/lib/orion/gems/$d.gem" ] || continue
    mkdir -p "$dir/lib/orion/gems"
    cp "$prefix/lib/orion/gems/$d.gem" "$dir/lib/orion/gems/"
  done
}

bundle_linux() {
  dir="$stage/out/$app"
  stage_tree "$dir"
  # Lua is the one runtime library desktop distributions do not ship by default.
  ldd "$dir/bin/$app" | awk '$1 ~ /^liblua/ && $3 ~ /^\// { print $3 }' | while read -r l; do
    cp -L "$l" "$dir/lib/"
  done
  printf '%s %s for Linux x86-64\n\nRun ./bin/%s\nRequires OpenGL, X11 or XWayland, and libxml2.\n' \
    "$app" "$version" "$app" > "$dir/README.txt"
  asset="$app-$platform.tar.gz"
  tar -C "$stage/out" -czf "$output/$asset" "$app"
}

bundle_windows() {
  dir="$stage/out/$app"
  stage_tree "$dir"
  # Copy every MinGW runtime DLL the executable and its plugins resolve.
  PATH="$dir/bin:$PATH" ldd "$dir/bin/$app.exe" | awk '$3 ~ /^\/mingw64\// { print $3 }' | sort -u | while read -r l; do
    cp "$l" "$dir/bin/"
  done
  printf '%s %s for Windows x64\r\n\r\nRun bin\\%s.exe\r\n' "$app" "$version" "$app" > "$dir/README.txt"
  asset="$app-$platform.zip"
  (cd "$stage/out" && zip -qr "$output/$asset" "$app")
}

bundle_macos() {
  share="$stage/share-$app"
  mkdir -p "$share"
  cp -R "$prefix/share/orion" "$share/"
  [ -d "$prefix/share/$src" ] && cp -R "$prefix/share/$src" "$share/"
  icon="apps/$src/share/icon.png"
  [ -f "$icon" ] || icon=images/logo.png
  name=$(python3 -c 'import sys; sys.path.insert(0, "packaging/macos"); import bundle; print(bundle.display_name(sys.argv[1]))' "$app")
  bundle="$stage/out/$name.app"
  python3 packaging/macos/bundle.py --app "$app" --source "$src" --bundle "$bundle" \
    --binary "$prefix/bin/$app" --runtime-libs "$prefix/lib" --share "$share" \
    --icon "$icon" --bundle-id "com.orion.$app" --version "$version" >/dev/null
  asset="$app-$platform.zip"
  ditto -c -k --sequesterRsrc --keepParent "$bundle" "$output/$asset"
}

: > "$output/SHA256SUMS-$platform.txt"
for app in $apps; do
  src=$(make -s app-source APP="$app")
  if [ ! -f "apps/$src/main.c" ]; then
    echo "skip    $app (not an application)"
    continue
  fi
  deps=$(awk -F '\t' -v a="$app" '$1 == a { print $3 }' packaging/packages.tsv)
  mkdir -p "$stage/out"
  "bundle_$os"
  rm -rf "$stage/out"
  sha256_line "$asset" >> "$output/SHA256SUMS-$platform.txt"
  echo "BUNDLE  $output/$asset"
done
