# Image Editor, Pencil Test, Jamsheet and Winamp on Android

Each app is one native library, `libmain.so`, linked statically like the iPad
apps, plus a small Java activity (`org.orion.OrionActivity`, a
`NativeActivity`). Gradle packages the library and the app's resources into a
debug-signed APK. The minimum is Android 9 (API 28).

## Requirements

- Android NDK r26 or later, with `ANDROID_NDK_HOME` set.
- Android SDK with platform 35 (`ANDROID_HOME`), Java 17 and Gradle 8.
- CMake and curl, for libxml2; a host C compiler, libxml2 and OpenGL headers, for `orionc`.

Together they take several gigabytes; GitHub Actions builds them for you.

## Build

```sh
make android APP=imageeditor                       # arm64-v8a
make android APP=groove ABIS="arm64-v8a x86_64"     # device and emulator
make android-all
adb install -r build/android/apk/orion-imageeditor.apk
```

`APP` is `imageeditor`, `penciltest`, `groove` or `winamp`. The APK lands in
`build/android/apk/orion-<app>.apk` with the application id `org.orion.<app>`
(`APPLICATION_ID=...` overrides it). `VERSION_NAME` defaults to `VERSION`.
`tools/android/libxml2.py` builds libxml2 2.13.5 from a SHA-256-pinned archive;
FreeType, libpng, libjpeg, zlib and the BMP libraries come from `vendor/`.
`tools/android/package.py` stages the resources, manifest and launcher icon.

## GitHub Actions

`.github/workflows/android.yml` builds all four APKs (arm64-v8a and x86_64) on
pushes to `main` that touch the framework, these apps or this packaging, and
when started by hand (Actions → Android → Run workflow). Each APK is an
artifact named `apk-<app>`. An API 34 emulator then installs Image Editor,
checks that it starts and stays up, and uploads `screen.png` and `logcat.txt`
as `android-smoke-imageeditor`. Releases (`release.yml`) attach the APKs.

## Runtime

- `android_main.c` unpacks the bundled files to private storage once per
  build, as `bin/../share/orion` and `share/<app>` like a desktop install,
  then runs the app's blocking loop (`GEM_STANDALONE_MAIN` names it
  `orion_main` on Android). The working directory and `HOME` are the app's
  external files directory, which is visible over USB.
- The platform backend (`platform/android`) drives `ALooper`, owns the EGL /
  GLES 3 surface and plays audio through AAudio. Sizes are density-independent
  points; `AX_PLATFORM_TOUCH` gives the iPad's finger-sized metrics.
- Text fields show the soft keyboard (`axSetTextInput`); the one-pixel input
  view in `OrionActivity` sends composed text to the app thread.
- stdout and stderr go to logcat: `adb logcat -s orion`.

## Not yet on Android

- No native Open / Save panels: the Orion file dialogs browse the app's own
  storage.
- The soft keyboard can cover fields near the bottom; the insets reach the
  platform (`axAndroidGetKeyboardInset`) but layout does not use them yet.
- One finger drives the pointer; two-finger scrolling is iPad-only so far.
- The APKs are debug-signed. Release signing is not set up.
