# Canvas branding

`groove.png` is the reel's existing transparent mascot and rainbow Fredoka
wordmark, exported together at 1536 × 512. Its colours and transparency are
preserved; the sheet draws it at 4.5% opacity beneath the grid and clips.
The imprint is capped at 700 logical pixels wide and scales to fit the canvas.
There are no added highlights, shadows or embossing effects.

Regenerate from the reel source with its existing Playwright setup:

```sh
python3 apps/groove/share/branding/export.py
```

The source mascot is `reel/motion/assets/icon-transparent.png`. Lettering uses
the reel's `sticker()` function, Fredoka Bold font and original rainbow palette
in `reel/motion/index.html`. Outlines and coloured faces are rendered on separate
layers, with every face above every outline so adjacent letters remain intact.
See `reel/README.md` for font attribution and setup.
