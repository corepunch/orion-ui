#!/bin/sh
# Rebuilds the Groove reel: soundtrack from the app's own engine, then the motion render.
# Run from anywhere; needs cc, python3 (numpy, scipy, playwright) and ffmpeg.
set -e
REEL=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$REEL/../../.." && pwd); OUT="$REEL/build"
mkdir -p "$OUT/blocks" "$OUT/inc"
G="$ROOT/apps/groove"
cp "$REEL/music/groove_shim.h" "$OUT/inc/groove.h"
cc -O2 -std=gnu99 -I"$OUT/inc" -I"$G" "$REEL/music/render_blocks.c" -lm -o "$OUT/render_blocks"
cd "$ROOT"                                   # the engine finds apps/groove/share/... from here
"$OUT/render_blocks" > "$OUT/blocks.tsv"     # library listing
IDS=$(python3 -c "
cats=['Drums','Kicks','Snares','Hats','Claps','Cymbals','Perc','Fills','Bass','Keys','Organ','Guitar','Synth','Pads','Stabs','Vocals','Scratch','FX']
rows=[l.split('\t') for l in open('$OUT/blocks.tsv')]
lib=[r[0] for c in cats for r in [x for x in rows if x[2]==c][:3]]
clips='522 201 501 508 105 502 511 523 526 435 505 520 521 22 495 500 503 509 514 515 524 527 529 86 112 108 496 531 303 200'.split()
print(' '.join(sorted(set(lib+clips),key=int)))")
BLOCKS_DIR="$OUT/blocks" "$OUT/render_blocks" $IDS
python3 "$REEL/music/compose.py"
python3 "$REEL/motion/capture.py" 0 1800 "$OUT/video.mp4" 4
ffmpeg -v error -y -i "$OUT/video.mp4" -i "$OUT/music.wav" -map 0:v -map 1:a -c:v libx264 -preset slow -crf 19 \
  -pix_fmt yuv420p -c:a aac -b:a 320k -shortest -movflags +faststart "$REEL/groove-reel.mp4"
echo "$REEL/groove-reel.mp4"
