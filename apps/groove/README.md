# Building Jamsheet

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

## Song sharing

**Share Song…** opens the system share sheet with an MP3. With a sharing
host configured, it can also create an unlisted browser listening link
and optionally include the editable project. See [hosting and verification](docs/share-links.md).

## Sample packs

Jamsheet's App Store builds include a permanent starter bank: 175 of the 583
built-in sounds, including all sounds used in the three demos. Every sound can
be auditioned. Sounds marked **Preview** require a pack before they can be
dragged from the library into a song. Recordings and clips already in a song
continue to work, including playback and export of imported projects.

**Sample Packs…** in the library opens the store. Drums & Fills, Bass, Guitar,
Synths, Pads & Stabs, Vocals, and Scratch & FX are each one-time purchases at
$0.99 in the U.S. or €0.99 in Germany. **Unlock Full Version** includes all six
at $4.99 or €4.99. StoreKit supplies the actual price for the player's storefront
and restores purchases from the same Apple Account.

Both shared Xcode schemes use [the local StoreKit catalog](store/Groove.storekit)
when run from Xcode. Use the StoreKit transaction manager to test an individual
pack, the full unlock, and refunds without charges. On a fresh test account,
audition and try to drag a Preview card; buy its pack and drag the same card
again. Verify that other packs still require a purchase, then verify the full
unlock, relaunch, and Restore Purchases. Use a signed TestFlight build to verify
the App Store Connect catalog and the sandbox purchase sheet and restore flow.

The seven products belong to [Jamsheet in App Store Connect](https://appstoreconnect.apple.com/apps/6821380428/distribution).
Attach a screenshot of **Sample Packs…** with the purchase buttons visible to
each product's App Review information. Submit the first purchases with the new
app build that contains the store. A free download price is required for the
intended free-to-try release.

## Apple release checks

Use the `groove-macos` and `groove-ipad` schemes from the linked Xcode
instructions. Test the exact signed builds that will be uploaded.

- On iPad, save a named song, reopen it from My Songs, and verify that Save
  updates the same song. Export a project copy to Files, import it, and save
  it to My Songs. Test with the onscreen keyboard in both orientations.
- On macOS, open and save songs in a folder outside the app sandbox using
  the native dialogs. Check the window close button and Command-Q with edits.
- On both platforms, cancel a switch away from an edited song, then save and
  switch. Reopen a project with an embedded recording and verify audio and
  video export. On iPad, also verify the microphone and Photos permission flows.
