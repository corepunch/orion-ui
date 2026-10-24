# Xcode projects

Generate Groove's native macOS and iPadOS targets with Python 3.9 or newer:

```sh
python3 tools/xcode/generate.py groove --unsigned
open apps/groove/groove.xcodeproj
```

Choose the `groove-macos` or `groove-ipad` scheme. The iPad scheme supports
Simulator and physical iPads (iPadOS 16+); the Mac target requires macOS 12+.
For device builds, select a team in **Signing & Capabilities** and keep automatic
signing enabled. No team or certificate is needed for macOS or Simulator builds.

The generator is opt-in and does not modify or invoke the Makefile. Each app's
project lives beside its sources: `apps/<app>/<app>.xcodeproj`. Commit the project,
its `Generated/` info plists and shared schemes so Xcode Cloud can discover it.
Xcode creates unity sources, the resource compiler and generated form headers in
DerivedData at build time, so a fresh checkout needs no pre-generated C files.
The source list is refreshed on every build, including newly added framework
modules such as localization. No build products are written into the app folder.
The normal `build/generated/` headers and Makefile-built binaries/libraries are
not used. Framework code is statically linked, using the SDK's libxml2; no
Homebrew packages, XcodeGen or CocoaPods are required. App resources, fonts,
shaders, demos and icons are bundled for running and debugging in Xcode.
Both targets use `AppIcon`, generated from `apps/groove/share/icon.png` in
DerivedData. Xcode compiles the asset catalog and merges its icon metadata into
the app's Info.plist; replacing the source PNG updates both targets on the next build.

Only Groove is supported initially. Regenerate to refresh the navigator after
adding/removing sources, or to update separately compiled common-dialog files.
Existing source edits and `.orion` changes rebuild normally. IDs and generated
files are deterministic; unchanged files retain their timestamps. With
`--unsigned`, regeneration preserves existing signing settings, bundle IDs and
shared schemes. Use `--settings-from /path/to/old.xcodeproj` to migrate those
settings, or `--output /path/to/folder` for a different project parent directory.

In Xcode Cloud, select this committed project and the `groove-ipad` or
`groove-macos` shared scheme. Cloud checks out public Git submodules automatically;
grant it access to `corepunch/platform` if that repository is private. See
[Apple's dependency setup](https://developer.apple.com/documentation/xcode/making-dependencies-available-to-xcode-cloud).
Choose a development team in Xcode before configuring a signed archive workflow.
No custom pre-clone or post-clone script is needed.

When the development certificate and profile used by `ipad-deploy` are installed,
omit `--unsigned` to configure manual signing with the exact same selector from
`tools/ipad/sign.py`. It picks the newest matching, unexpired development profile
with an available signing identity. The default bundle ID is `com.orion.groove`.
Overrides are `--team`, `--profile`, `--bundle-id`; `TEAM` and `PROFILE` environment
variables are also accepted. Certificates and profiles are never copied into the
generated project.

Build from the command line:

```sh
xcodebuild -project apps/groove/groove.xcodeproj \
  -scheme groove-macos -derivedDataPath build/xcode/DerivedData build
xcodebuild -project apps/groove/groove.xcodeproj \
  -scheme groove-ipad -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath build/xcode/DerivedData-ipad build
# Compile for a device before configuring signing:
xcodebuild -project apps/groove/groove.xcodeproj \
  -scheme groove-ipad -destination 'generic/platform=iOS' \
  -derivedDataPath build/xcode/DerivedData-device CODE_SIGNING_ALLOWED=NO build
```
