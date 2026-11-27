---
layout: default
title: Package Manager
nav_order: 3
permalink: /package-manager/
---

# Orion Package Manager

The `orion` package manager installs the Orion SDK and runtime by default, then
installs Orion applications independently from GitHub Release artifacts. Its
commands follow the familiar Homebrew model while keeping the implementation
small, auditable, and usable from POSIX shells.

## Install Orion

Bootstrap the package manager, development headers, libraries, and shared
runtime on macOS or Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/corepunch/orion-ui/main/install.sh | sudo sh
export PATH="/opt/orion/bin:$PATH"
```

The default prefix is `/opt/orion`. For an installation owned by the current
user, no administrator permission is required:

```sh
curl -fsSL https://raw.githubusercontent.com/corepunch/orion-ui/main/install.sh |
  ORION_PREFIX="$HOME/.local" sh
export PATH="$HOME/.local/bin:$PATH"
```

Set `ORION_PREFIX` on later commands when using a non-default prefix:

```sh
ORION_PREFIX="$HOME/.local" orion install scener
```

## Commands

```sh
# Browse available applications and tools
orion search
orion search editor
orion info scener

# Install one or more packages
orion install scener
orion install imageeditor terminal

# Inspect and maintain installed packages
orion list
orion update
orion uninstall scener

# Package-manager version and command summary
orion --version
orion --help
```

`orion remove` is an alias for `orion uninstall`, and `orion upgrade` is an
alias for `orion update`.

## Package Model

`orion-core` is installed during bootstrap and contains:

- The `orion` package-manager executable.
- Orion and Platform development headers.
- Orion and Platform linkable runtime libraries.
- Shared framework assets.
- Core offline documentation.

Every application and developer tool is a separate release archive. An
application package can contain its standalone executable, loadable GEM,
component plugin, application assets, and documentation. Installing one
application therefore does not install the complete suite.

Dependencies are resolved from the release index. For example,
`orion install shell` also installs the FileManager and Terminal packages that
the Shell loads as GEMs.

Package receipts are stored under `<prefix>/.orion/installed`. They record the
installed version and owned files so packages can be listed, updated, and
removed without deleting files owned by another package.

## Build Applications With The SDK

The bootstrap installation is also an Orion development SDK. Include headers
from `<prefix>/include` and link libraries from `<prefix>/lib`:

```sh
cc app.c -o app \
  -I/opt/orion/include \
  -L/opt/orion/lib \
  -lkernel -lcommctl -lcommdlg -luser -lplatform
```

Use `$HOME/.local/include` and `$HOME/.local/lib` when Orion was installed with
`ORION_PREFIX="$HOME/.local"`. Orion and its native Platform layer require no
additional third-party libraries. Add Lua only when an application uses
scripting or the interactive terminal.

## Build a macOS App Bundle

On macOS, build a Finder-launchable `.app` bundle for any standalone app with a
PNG icon in its `share` directory:

```sh
make mac-deploy APP=terminal
make mac-deploy APP=gitclient
```

The output is `build/macos/<app>.app`. The target builds the app, bundles its
Orion and app resources and non-system dynamic libraries, converts
`apps/<app>/share/icon.png` (or the Orion logo for apps without one) to the
Finder `.icns` icon, and ad-hoc signs the bundle for local launch. Override the bundle identifier with
`BUNDLE_ID=com.example.myapp`.

The primary headers are:

```c
#include <orion/ui.h>
#include <orion/gem.h>       /* Loadable GEM applications */
#include <platform/platform.h>
```

Header subdirectories are preserved, so includes used throughout the source
tree work unchanged in installed application projects.

## Security And Releases

Before extracting an archive, `orion` verifies its SHA-256 checksum against the
platform package index and rejects archive paths that are absolute or contain
parent-directory traversal.

Tagged releases publish separate packages for each supported platform:

| Platform | Identifier |
|---|---|
| macOS on Apple silicon | `macos-arm64` |
| Linux on x86-64 | `linux-x86_64` |
| Windows on x64 (MinGW) | `windows-x86_64` |

The default channel follows the latest GitHub release. Pin a specific release
when reproducibility is required:

```sh
ORION_RELEASE=v1.0.1 orion install scener
```

The release page contains one archive per package and platform, plus a
checksummed `packages-<platform>.tsv` index. SDK-enabled packages are available
from [Orion v1.0.1](https://github.com/corepunch/orion-ui/releases/tag/v1.0.1)
and later.

## Environment Variables

| Variable | Purpose | Default |
|---|---|---|
| `ORION_PREFIX` | Installation root | `/opt/orion` |
| `ORION_RELEASE` | Release tag or `latest` | `latest` |
| `ORION_REPOSITORY` | GitHub owner and repository | `corepunch/orion-ui` |
| `ORION_RELEASE_BASE` | Complete release asset URL override | unset |

`ORION_RELEASE_BASE` is useful for mirrors and offline test servers. When set,
it takes precedence over `ORION_RELEASE` and `ORION_REPOSITORY`.

## Installed Layout

```text
<prefix>/bin/                 package manager, applications, and tools
<prefix>/include/orion/       Orion development headers
<prefix>/include/platform/    Platform development headers
<prefix>/lib/                 linkable libraries and component plugins
<prefix>/lib/orion/gems/      loadable Orion applications
<prefix>/share/orion/         framework assets
<prefix>/share/<app>/         application assets
<prefix>/share/doc/orion/     framework and application documentation
<prefix>/.orion/installed/    package receipts
```

## Building Release Packages

Git Client and Ledger use the shared [Orion release](https://github.com/corepunch/orion-ui/releases/tag/v1.0.11).
Scener follows the same process. The [Platform Builds workflow](https://github.com/corepunch/orion-ui/blob/main/.github/workflows/platform-builds.yml)
builds the desktop matrix and runs package checks. The [Release Packages workflow](https://github.com/corepunch/orion-ui/blob/main/.github/workflows/release.yml)
calls it with the version in `VERSION`, collects the artifacts and stages a draft.

| Asset | For |
|---|---|
| `orion-<package>-<version>-<platform>.tar.gz`, `packages-<platform>.tsv` | the `orion` package manager |
| `<app>-<platform>.zip` / `.tar.gz`, `SHA256SUMS-<platform>.txt` | people downloading one app |

### Prepare an app for release

1. Register the app slug in `packaging/packages.tsv` and declare any Orion app
   dependencies there. For example, Git Client depends on Terminal. Check that
   `make install` stages the resources needed in the extracted archive.
2. Provide a square 1024 × 1024 PNG at `apps/<app>/share/icon.png` for macOS.
   The bundler converts it to `.icns` and uses `com.orion.<app>` as the bundle ID.
3. If the Mac ZIP will be advertised as notarized, add the slug to `APPS` in
   `packaging/macos/notarize.py` and to the three-platform asset check in
   `.github/workflows/release.yml`. Add checks to the platform workflow for
   any app-specific files or runtime behavior. Scener checks that the OBJ and
   GLB exporter plugins load from each extracted archive, and captures the
   editor opening a packaged scene on Linux.
4. Run Platform Builds manually on the candidate branch to check all three
   targets before cutting the release.

### Sign and publish a release

1. Commit the new `VERSION` and release changes. Manually run Release Packages
   on the release branch with the version without the `v` prefix, or push a matching
   `v*` tag. Wait for the draft workflow run to succeed and record its run ID.
   Use the full Linux test suite for Scener. The `desktop_checks_only` input
   limits Linux tests to the Git Client, Ledger and related framework suites.
2. On the Mac with the Apple Developer team configured in Xcode and `gh`
   authenticated for the repository, run `make mac-notarize MAC_RELEASE_RUN=<build-run-id>`
   from the release checkout. It downloads the signing inputs from that exact
   run, uses Xcode's Developer ID cloud signing, waits for Apple's tickets and
   uploads the verified ZIPs to the draft. It keeps its output under
   `build/macos-notarized/<build-run-id>/` and reuses completed submissions on retry.
   Set `MAC_NOTARY_TEAM` both locally and in the Actions repository variables
   to use a different Apple team.
3. Run Release Packages again with the same version and set `publish_run` to
   the original build run ID. The macOS runner verifies the uploaded ZIPs'
   bundle IDs, version, architecture, Developer ID signatures, stapled tickets
   and Gatekeeper status. It regenerates the macOS checksums and verifies the
   complete set of assets before publishing the draft.
4. Confirm the three files and SHA-256 manifests are public on the release.
   Deploy `docs/` from the release checkout with the **Deploy Docs to GitHub
   Pages** workflow and open the app's public page to verify each download.
   For a new download page, use manual release dispatch and deploy Pages after
   publication. Pushing a tag starts both workflows independently.

Release archives can be generated locally after building the repository:

```sh
sh scripts/package-release.sh 1.0.1 macos-arm64 dist
```

The package catalog is `packaging/packages.tsv`. Each row declares the package
name, description, and optional dependencies. `scripts/package-release.sh`
stages the normal `make install` output and splits it according to that catalog.

## Downloadable App Builds

`scripts/bundle-apps.sh` turns every application in `packages.tsv` into a
portable archive that runs without installation:

| Platform | Asset | Contents |
|---|---|---|
| Windows | `<app>-windows-x86_64.zip` | `<app>\bin\<app>.exe` with the Orion and MinGW DLLs beside it, `share\` |
| macOS | `<app>-macos-arm64.zip` | `<Name>.app`, built like `make mac-deploy` |
| Linux | `<app>-linux-x86_64.tar.gz` | `<app>/bin`, `<app>/lib` (Orion libraries, Lua), `<app>/share` |

Local macOS bundles use ad-hoc signing. The Mac ZIPs listed in the notarization
script use Developer ID signing and Apple notarization in the public release.
Windows SmartScreen may ask for confirmation.

Build them locally for the current platform after `make`:

```sh
make bundles                           # every app, into build/bundles
make bundles BUNDLE_APPS="ledger terminal"
```

A new app gets a download by adding a row to `packages.tsv`. The portable
bundler skips developer tools such as `orionc` that lack an app entry point.

### Download links and icons on the app page

- Add a download section to `docs/<app>/index.html` with three visible links
  for Windows x64, macOS on Apple silicon and Linux x86-64. Keep short launch
  instructions and a link to the release notes and checksums below the buttons.
- Git Client and Ledger use `/releases/latest/download/<asset>` to follow the
  newest Orion release. For a page that should point to a specific verified
  build, use `/releases/download/v<version>/<asset>` and link to that tag's
  release notes. Scener uses this form.
- Copy the inline platform SVGs from `docs/gitclient/index.html` or
  `docs/ledger/index.html`. These are Phosphor `windows-logo-fill`,
  `apple-logo-fill` and `linux-logo-fill` from revision `2b75f3a`. Preserve the
  paths and `viewBox`, use `fill: currentColor` with a 24 px display size, and
  set `aria-hidden="true"` because the link already has a visible label.
  Keep the revision comment and the [Phosphor license](https://github.com/corepunch/orion-ui/blob/main/docs/ledger/PHOSPHOR-LICENSE).
- The product icon can also serve as the favicon and page brand. Git Client
  uses `shots/icon.png` in the tab, header and download section. Ledger uses
  a debit mark for the header, and the same Phosphor platform logos for its
  Russian and German downloads.

## Installing The Complete Suite From Source

The package manager is intended for released, on-demand installations. To
install every application, GEM, tool, runtime library, asset, and document from
a source checkout instead, use:

```sh
make all
sudo make install
```

For a user-local source installation:

```sh
make install PREFIX="$HOME/.local"
```
