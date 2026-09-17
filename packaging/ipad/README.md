# Image Editor and Pencil Test on iPad

These are separate, statically linked iPad apps built directly with Xcode SDK
commands, following `books`' Makefile workflow. No Xcode project or third-party
iOS dependencies are required. The default deployment target is iPadOS 16.

```sh
make ipad-all                         # both device bundles
make ipad APP=imageeditor               # one device bundle
make ipad-simulator APP=penciltest
make ipad-run APP=penciltest             # build, install, launch in Simulator
make list-devices
make ipad-deploy APP=imageeditor         # auto-select the only connected iPad
make ipad-deploy APP=penciltest          # auto-select the only connected iPad
make ipad-deploy APP=penciltest DEVICE="iPad name or identifier"  # explicit device
make ipad-mac APP=penciltest             # signed iPad build on Apple silicon Mac
```

`APP` defaults to `imageeditor`. If `DEVICE` is omitted for deployment,
the Makefile auto-selects the sole connected iPad and fails if there are zero
or multiple iPads. `DEVICE` may also select a device or simulator by name or
UDID. Device installation uses an installed development certificate and a
matching provisioning profile; optionally supply `TEAM=...`, `PROFILE=...`,
and `BUNDLE_ID=...`. Defaults are `com.orion.imageeditor` and
`com.orion.penciltest`, so the apps coexist and have independent Documents and
settings directories. Use a distinct bundle identifier for each app.

Outputs are `build/ipad/<SDK>-<ARCH>/<app>/<app>.app`. Advanced builds can invoke
`make -f packaging/ipad/build.mk APP=penciltest SDK=iphoneos IOS_MIN=16.0 app`
and override `BUILD_DIR` or `ARCH`. The `orionc` resource compiler runs on the
host; all app and framework code is compiled with the selected iOS SDK.

The UIKit backend lives in the `platform` submodule, under `platform/ios`.
Commit changes in that submodule as well as the parent repository when saving
this port. See `platform/ios/README.md` for the native lifecycle contract.

The app fills its scene's safe area and lays out its document viewport and
timeline when the display changes. Rotation resizes the view, preserving the
artwork's pixel dimensions. One finger or Apple Pencil draws; Pencil movement
includes coalesced samples. Two-finger dragging scrolls through the existing
framework routing. Hardware shortcuts use Orion accelerators. Text controls
request the software keyboard.

Each display callback drains queued input, including wakeup sentinels, before
painting once. Window dragging and resizing defer painting to that frame;
all coalesced touch samples remain available for drawing strokes.

Open uses the system document picker to import files. Save uses Orion's file
picker, starting in the app's writable Documents directory. Documents are
visible in Files and through file sharing. Framework and app resources are
bundled under `share/`; dynamic editor components are linked and registered
statically on iOS.

## Icons

`icons/penciltest.png` uses the top animation artwork and
`icons/imageeditor.png` uses the bottom brush artwork from the supplied image.
They were extracted with the built-in imagegen tool. The edit prompts asked to
preserve each icon's artwork and colors, remove the labels and surrounding
page margin, and extend its tile background to square edges for the iPad mask.
`tools/ipad/bundle.py` produces the required icon sizes with `sips` and compiles
the asset catalog with `actool`.
