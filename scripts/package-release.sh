#!/bin/sh
set -eu

[ $# -eq 3 ] || { echo "usage: $0 VERSION PLATFORM OUTPUT_DIR" >&2; exit 2; }
version=$1
platform=$2
output=$3
stage=$(mktemp -d "${TMPDIR:-/tmp}/orion-package.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
prefix="$stage/opt/orion"
mkdir -p "$output"

make install DESTDIR="$stage" PREFIX=/opt/orion

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}

archive_package() {
  package=$1
  payload=$2
  description=$3
  dependencies=${4:-}
  if [ "$package" = orion-core ]; then asset="orion-core-$version-$platform.tar.gz"
  else asset="orion-$package-$version-$platform.tar.gz"; fi
  tar -C "$payload" -czf "$output/$asset" .
  checksum=$(sha256_file "$output/$asset")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$package" "$version" "$platform" "$asset" "$checksum" "$description" "$dependencies" >> "$output/packages-$platform.tsv"
}

payload="$stage/core"
mkdir -p "$payload/bin" "$payload/lib" "$payload/share"
install -m 755 packaging/orion "$payload/bin/orion"
cp "$prefix/bin/orion-run" "$payload/bin/"
cp "$prefix"/lib/libkernel.* "$prefix"/lib/libuser.* "$prefix"/lib/libcommctl.* "$prefix"/lib/libcommdlg.* "$prefix"/lib/libscript.* "$prefix"/lib/libplatform.* "$payload/lib/"
cp -R "$prefix/include" "$payload/"
cp -R "$prefix/share/orion" "$payload/share/"
mkdir -p "$payload/share/doc/orion"
cp "$prefix/share/doc/orion/README.md" "$payload/share/doc/orion/"
cp "$prefix/share/doc/orion/package-manager.md" "$payload/share/doc/orion/"
archive_package orion-core "$payload" "Orion package manager, SDK, and runtime"

while IFS="$(printf '\t')" read -r app description dependencies || [ -n "$app" ]; do
  [ -n "$app" ] || continue
  payload="$stage/app-$app"
  mkdir -p "$payload/bin" "$payload/lib/orion/gems" "$payload/share/doc/orion/apps"
  cp "$prefix/bin/$app" "$payload/bin/"
  [ -f "$prefix/lib/orion/gems/$app.gem" ] && cp "$prefix/lib/orion/gems/$app.gem" "$payload/lib/orion/gems/"
  for plugin in "$prefix/lib/${app}_components."*; do
    [ -f "$plugin" ] && { mkdir -p "$payload/lib"; cp "$plugin" "$payload/lib/"; }
  done
  [ -d "$prefix/share/$app" ] && { mkdir -p "$payload/share"; cp -R "$prefix/share/$app" "$payload/share/"; }
  [ -d "$prefix/share/doc/orion/apps/$app" ] && cp -R "$prefix/share/doc/orion/apps/$app" "$payload/share/doc/orion/apps/"
  archive_package "$app" "$payload" "$description" "$dependencies"
done < packaging/packages.tsv