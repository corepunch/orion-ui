# Building Groove

The default Make build does not require `third_party/shine/`. WAV, M4A (AAC),
and MP4 export remain available through their existing backends. MP3 import
uses minimp3 and is independent of Shine.

MP3 export is optional. With `third_party/shine/` present, define
`GROOVE_USE_SHINE` (`-DGROOVE_USE_SHINE`) to compile the encoder and expose
MP3 in the export dialog:

```sh
make -B build/bin/groove GROOVE_USE_SHINE=1
```

Use `GROOVE_USE_SHINE=0` to build without it. Pass `-B` when switching this
option so Make rebuilds outputs compiled with the previous flags.
The same option is accepted by `packaging/ipad/build.mk`.

The committed Xcode project and its generator define `GROOVE_USE_SHINE` for
both macOS and iPadOS, including Xcode Cloud builds. GitHub Actions also
enables it for builds, tests, and release packages. These builds require the
vendored Shine folder. To build without Shine in Xcode, remove the definition
from the project's **Preprocessor Macros** setting. See
[the Xcode instructions](../../tools/xcode/README.md).

Shine's upstream revision, license, and local changes are documented in
`third_party/shine/README.orion.md` when that optional folder is present.
