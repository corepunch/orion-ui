# Comics

Open *The Boat in the Fog* from the shelf, or open its CBZ in Comics from Files
on iPad. The shelves show the bundled book and CBZs in **On My iPad → Comics**.
Use **Open CBZ** to open a file and add it to the shelf. Comics uses the first
page as the cover when there is no separate cover artwork. An app-rendered
spine and shadow give the covers a hardcover look.

Tap a cover to open the book; it grows out of the shelf, and the bookshelf
button shrinks it back into its cover. Drag the artwork left or right to turn
pages or spreads: the next page follows your finger, and on release it settles
on whichever page is nearer, carried by the speed of the swipe. In landscape the
first page lies alone on the right, as a cover does in an open book.

Each pair of story pages has one line-art panel to color. Pick a color at the
top of the screen and paint with a finger or Apple Pencil. The color goes on as a
watercolor wash under the ink and is kept with your progress.

[The Boat in the Fog CBZ](share/books/the-boat-in-the-fog.cbz) contains a cover
and twenty lettered English story pages for standard CBZ readers. In Comics, it
also includes interactive watercolor coloring and English and Russian dialogue.
Copies of the full issue share the same progress. Comics carries over the page
position and watercolor strokes from the five-page preview on first open.

Comics reads stored and deflated JPEG, PNG, and BMP pages in natural filename
order. It fits each page to the available space and shows two pages in landscape.
Archives can have up to 4,096 pages, 128 MiB total, and individual encoded
images up to 32 MiB.

## Publishing the comic

Run `python3 apps/comics/tools/build_fog_cbz.py` with Pillow to rebuild the CBZ
from `apps/comics/art/fog-comic`. The XML supplies the cover position, panel order,
dialogue and the panels to color. Each panel can supply translated `text`
elements, with English as the fallback. The added art sheets were drawn with
[these ImageGen prompts](art/fog-comic/imagegen-prompts.md).

```xml
<cover />
<page>
  <panel shot="knots" paint="true">
    <text lang="en">I can tie knots with my eyes closed, too.</text>
  </panel>
</page>
```

- `<cover />` displays the first JPEG by itself. The following pages are paired
  in landscape.
- `paint="true"` displays line art to color. The reader's colors belong to the
  shot, so the same coloring panel later in the story shows them again.

Comics uses the ordinary JPEG pages when the interactive data is missing,
invalid or unsupported.

### Moving a speech bubble

On the relevant `<panel>` in `orion/story.xml`, add the placement attributes
you need. The same values apply to every translation of the line.

```xml
<panel shot="lantern" bubble-edge="top" bubble-x="75" bubble-inset="25">
  <text lang="en">In this fog? Shine the lantern on it first.</text>
  <text lang="ru">В таком тумане? Сначала посвети фонарём.</text>
</panel>
```

- `bubble-edge` accepts `top` or `bottom`. Story dialogue defaults to the
  bottom; the harbor's line defaults to the top.
- `bubble-x` is the horizontal anchor, from `0` at the left to `100` at the
  right. The bubble stays inside its available space.
- `bubble-inset` moves the bubble from its chosen edge toward the center.
  `0` is the default and `100` uses all the available travel. On the harbor
  plate the bubble travels only through the upper half, above the boats.

Save the XML at the same path inside the CBZ. Open that file from Files or with
**Open CBZ** in Comics to see the change. The publishing script applies these
attributes when it regenerates the JPEG pages for other readers.
