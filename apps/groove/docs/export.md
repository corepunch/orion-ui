# Audio and timeline video export

File > Export Audio (Ctrl+E) saves WAV (stereo PCM16, 44.1 kHz), MP3
(192 kbps CBR), or M4A (AAC). File > Share Audio defaults to M4A and opens
the native share sheet on iPad or sharing picker on macOS. Save File is
available in the same dialog. Files made for sharing remain in
`axSettingsDirectory()/Exports` so asynchronous recipients can read them.
On iPad, Save File saves in Documents, accessible in Files.

File > Export Timeline Video (Ctrl+Shift+E) makes MP4 with H.264 video and
AAC audio at 30 fps: 16:9 = 1280×720, 21:9 = 1680×720. Both are exact
ratios with square pixels. The image comes from the actual arrangement:
eight tracks, bar ruler, original block names and sample waveforms, with
a playhead driven by audio sample time. It is a complete-song overview,
independent of the visible viewport, screen size and live transport.
The same overview renderer draws saved-song thumbnails. Muted tracks
are dimmed. Mono instruments remain mono within a stereo output file.

Exports start at zero and end at the arrangement's effective last clip,
including deliberate gaps. Loop and library/recording audition are excluded.
Mute, solo, per-clip gain, source playback, repetitions and overlapping-clip
cutoffs use the playback mixer. The final tanh master conversion is shared
with the audio callback. WAV is exact PCM; MP3/AAC are lossy and MP3 adds
normal codec padding. Recorded samples already embedded in songs are mixed
like the stock library.

The dialog stays responsive, supports cancellation, and joins its worker
before freeing resources. A private source-audio snapshot protects exports
from concurrent transport/library changes. Memory contains source samples
and a static video canvas, never the whole rendered song or movie. Staging
files are removed on failure or cancellation. An existing destination is
replaced only after successful encoding. Export never changes the active
project filename or transport position.

Apple AAC/H.264 encoding and muxing use AVFoundation; no external encoder,
network service or social-account login is needed. Portable MP3 uses the
vendored Shine encoder (upstream revision and LGPL license in
`third_party/shine`). Linux/Windows AAC and MP4 require ffmpeg on PATH.
The desktop backend passes paths as process arguments, without a shell, and
loops one static frame while overlaying the audio-clocked playhead; it does
not spool an uncompressed movie. Native sharing falls back to a saved file
and explains how to use Save File on other platforms.

Validation: Groove's tests check finite WAV duration, bit-exact playback PCM,
unchanged transport, cancellation cleanup, MP3 decode, and both playhead
ratios. Set `GROOVE_MEDIA_TEST_OUTPUT=/tmp/groove-media` when running
`build/bin/test_groove_export_test` for real AAC and MP4 backend integration files;
this optional check needs ffmpeg on non-Apple desktops. Use ffprobe to inspect
codecs, dimensions and stream durations. CI also runs a standalone AVFoundation smoke test for AAC, both H.264 sizes,
duration and cancellation. Native share-sheet/device checks
must also be performed on a signed iPad build.
