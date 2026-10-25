# Recorded samples and portable songs

Groove keeps eight tracks and mono instruments. File > Record Sample records
the default microphone into a reusable one-, two-, or four-bar sample at the
current song tempo. Recording starts only after a Record action and microphone
permission. The first permission request does not start recording: press Record
again after granting access.

Use headphones while playing the song. Input is not monitored through the
speakers. Recording stops automatically at the chosen length; Stop ends capture
early and leaves silence for the rest of the block. Listen auditions the draft.
Save Sample writes the original and makes it available under My Samples. Cancel
discards the draft. There is no automatic insertion into an arrangement: drag
the new sample to the desired position and track.

## Storage

Originals live in `axSettingsDirectory()/Samples`, beside the iPad `Songs`
directory. They are standard mono 44.1 kHz PCM16 WAV files with a private `gruv`
metadata chunk for the name, tempo and bar count. Titles are never used as paths.
The sample library deduplicates equal names, metadata and PCM. It is loaded at
startup, so recordings can be reused in other songs.

Songs containing only built-in blocks keep the existing `ORION_GROOVE 4` format.
A song using a recording is saved as `ORION_GROOVE 5`. Each distinct custom
source is embedded once, regardless of the number of placements. Built-in
sources remain references by ID and name. Versions 2–4 remain readable; older
Groove builds cannot open version 5 songs.

Version 5 uses the existing song header and clip rows. An asset is either the
existing `B <id>\t<name>\n` record or `S\n`, a complete bounded WAV, and `\n`.
Clip rows retain the version 4 fields. PCM is explicitly little-endian. The
loader validates the complete file, metadata, bounds and clip references before
installing its sources or replacing the active song. Truncated or invalid files
leave the active song intact. Custom PCM is capped at 64 MiB in the in-memory
library and per document; catalogue and clip limits still apply.

Opening a version 5 song restores the recordings from the document, makes them
available in My Samples and tries to keep copies in the local Samples library.
An unavailable local library does not prevent playback of an embedded source.
At a different song tempo, recordings retain their original pitch and speed;
the block is cropped or padded to its new musical duration. Independent time
stretching is not part of this change.

## Sending and receiving projects

File > Export Project writes a self-contained copy without changing the active
song's save location. On iPad the existing native save dialog places it in the
app's Documents area, where Files can share it. Send the `.groove` file; no
separate WAV folder is needed. File > Import Song opens a received project
through the native picker. On iPad, Save Song As adds it to My Songs.

Project export is distinct from audio mixdown. It needs Groove at the receiving
end. WAV/MP3 mixdown, native Share and social video remain separate proposals.

## Capture and packaging

The reusable `orion/kernel/audio_input.h` API has an Apple backend using
AVFoundation permission requests and AudioQueue input. Its mono S16 callback
copies into a preallocated bounded buffer, with atomic publication of the write
position; it does not allocate, write files or call UI code. Close synchronously
stops callbacks before the draft is freed. iPad temporarily uses a play-and-record
session and restores its previous category and mode afterwards.

The recording dialog is declared in `groove.orion`; its controller owns the
draft, capture device, timer and playback preview. macOS/iPad packaging includes
the microphone usage description, AVFoundation linkage and the macOS audio-input
sandbox entitlement. Other platforms report capture as unavailable; project
loading, sample playback and WAV persistence remain portable.

## Validation

The Groove test suite includes a round trip that saves two placements of one
recording, deletes the standalone WAV, opens the song from embedded PCM, checks
the exact restored samples and renders audible output. Reopening deduplicates
the source. Truncated samples and trailing project garbage are rejected without
changing the active song. Existing version 2–4 project tests remain in place.

Before release, test on real macOS and iPad hardware: initial permission, denied
permission and Settings recovery, built-in and external microphones, Stop,
automatic completion, Listen, Cancel during capture, repeated takes, audio
interruptions and importing/exporting through Files. These checks cannot be
replaced by the Linux tests or packaging validation.
