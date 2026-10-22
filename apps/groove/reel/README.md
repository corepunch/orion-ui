# Groove reel

`groove-reel.mp4` — a 30-second, 1080p60 motion piece introducing Groove.

Everything in it comes from this app:

- **Music**: Groove's own engine (`synth.c`, `library.c`, the sf2 bank and the vocal
  MP3s) renders library blocks at 140 BPM; `music/compose.py` arranges them into a
  17.5-bar edit (intro → verse → snare build → drop → hit) and masters the mix
  (sidechain pump, intro filter sweep, width, limiter to −1 dBFS, about −12.5 LUFS).
- **Picture**: the app icon cut into character / headphones / spark layers, the
  category pictogram atlas, `category_color()` colours, Phosphor transport icons,
  and cards drawn from real block names and rendered waveforms. The app window is a
  reconstruction of the 1180×760 layout, not a screenshot.
- **Sync**: `compose.py` also writes `motion/sync.js` — per-track envelopes at 60 fps,
  kick/snare/vocal onsets and the clip layout. The motion page reads it, so cuts land
  on downbeats, letters on 16ths, clips drop on the snare roll, and the playhead is
  the actual song position.

## Rebuild

```sh
apps/groove/reel/build.sh
```

Needs `cc`, `ffmpeg`, and Python 3 with `numpy`, `scipy` and `playwright` (Chromium).
Intermediate files go to `reel/build/` (ignored). The script:

1. compiles `music/render_blocks.c` — the app's audio sources with `groove_shim.h`
   standing in for `groove.h`, so no UI framework is needed — and renders the blocks;
2. runs `music/compose.py` → `build/music.wav` and `motion/sync.js`;
3. renders `motion/index.html` frame by frame through headless Chromium
   (`motion/capture.py`, 4 sub-frames of motion blur per frame);
4. muxes picture and sound into `groove-reel.mp4`.

To preview a single frame, open `motion/index.html` in Chromium with file access
enabled and call `renderFrame(seconds)` in the console.

Fonts: Fredoka and Inter (SIL Open Font License), from Fontsource.
