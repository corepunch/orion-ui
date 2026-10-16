# Pencil Test background plates

Stock animation backgrounds for Pencil Test. They live in `apps/imageeditor/penciltest/share/` as JPEG files and ship in `share/imageeditor/penciltest/`. Layer > Background Image... lists them as square thumbnails from `share/thumbs/`; choosing one makes it the shared Background picture.

When asked to "draw N backgrounds" and pointed at this file, follow it exactly. Do not invent a smoother, more rendered look.

## Output contract

- Count: however many new locations were asked for. Do not replace an existing file unless asked to redraw it.
- Size: 1280×720, 16:9, full-bleed. Paint touches all four edges.
- File: JPEG, RGB, quality 92, optimize, mode `0644`. Keep this. Quality 20 was rejected as too soft, and quality 50 was rejected after the files were restored.
- Name: `kebab-case` of the place, one plate per file, in `apps/imageeditor/penciltest/share/`. Example: `wheat-field.jpg`.
- One original location per file. No contact sheets, no captions, no borders, no signatures.
- Add each new filename to the catalog at the bottom of this file.
- Regenerate the browser thumbnails with `make -C apps/imageeditor/penciltest thumbs`: a centered square crop, area-averaged in linear light to 128×128, JPEG quality 85.

## What they are for

A character will be drawn on top. The plate is a stage, not a poster.

- The lower half is an empty place to stand: path, floor, yard, platform, sand, snow, deck.
- No people, no animals, no faces, no creatures, no vehicles with drivers.
- No readable words: no signs, titles, book spines you can read, clock numerals, flags with symbols, watermarks, or signatures.
- One clear ground plane. The camera is standing height, looking slightly into the place, the way a feature-animation background is staged.
- Keep the middle of the stage quieter than the edges, so a figure will read.

## The paint

These are traditional animation backgrounds: gouache on cold-press watercolor paper, the kind a background department paints for a theatrical feature. Matte. Slightly uneven. The sheet should look brushed, not rendered.

What has to be visible:

- Bristle marks in bark, stone, wood, grass, and snow.
- Leaves, flowers, and roof tiles built from separate dabs of paint, not smooth blobs.
- Soft wet blends only in the far distance and the sky.
- Paper tooth showing through thinner passages.
- Pigment that changes across a wall or a field, the way a loaded brush runs out.

What the first attempt got wrong, and must not come back:

- Smooth digital illustration, glossy light, sparkles, and airbrushed glow. That set was rejected for not looking painted.
- A hard colored outline around trees, posts, leaves, or crates. A gold or violet "rim" drawn around the silhouette is a defect, not light.
- A white or tan paper mat, deckled edge, or photographed sheet around the painting. The paint fills the rectangle. A margin is cropped off before saving.
- Copying a reference scene. Film stills and concept paintings (twisted ravines with rope bridges, treasure grottos, playrooms, toy workshops) are not inputs. They leak their layout, their characters, and their lettering.
- Neon sunbeams that look pasted on. Shafts of light, when used, are thin washes in the sky, not a graphic overlay.

Anchor every new plate with `image_edit` from an existing file in this folder. Do not start from a fresh text-only generation, and do not pass outside reference images.

- Outdoor places: `share/forest-glade.jpg`
- Rooms and halls: `share/cottage-hearth.jpg`

One anchor image is enough. The editor accepts at most three, and extra inputs make it copy their layout.

## Light

Name one light source and where it sits (late sun at the right, moon overhead, fire in the hearth, a high overcast).

Rim light is a thinner, lighter stroke of the same paint on the lit side of a form: the sunlit cheek of a trunk, the top edge of a roof, the window side of a beam, the lantern side of a post. It is part of that object. It is not a second color drawn around the outside.

Use it where the light would actually catch an edge. A foggy or overcast plate can have a soft pale edge instead of a hard one. Do not put a rim on everything.

Good pairs already in the set: warm fire against cool window light in `cottage-hearth.jpg`, copper along the mesa tops in `desert-canyon.jpg`, lantern orange brushed into the planks in `harbor-dusk.jpg`.

## Prompt

Use this shape, filled in, as the whole `image_edit` prompt. Two to five sentences. Describe what is in the picture. Do not list exclusions in the prompt; the empty stage is "an empty stage" and "paint fills the frame."

```
Keep this gouache-on-watercolor-paper handling: matte pigment, visible brush dabs, paper tooth, and soft hand-blended distance. Repaint the sheet as [place, with the empty ground described]. [Light source] is brushed into the lit side of [two or three forms] as a thinner lighter edge of the same paint. The [path or floor] is an empty stage, and the paint fills the frame edge to edge.
```

Set `aspect_ratio` to `16:9`. A single-image edit keeps the anchor's aspect, so the anchor must already be 1280×720.

Each call is one new place. Change the place, the time of day, and the light. Do not make another forest, village square, castle court, cove, cottage, night garden, desert canyon, snow lane, jungle pool, or harbor if that file is already in the catalog.

## Procedure

1. Read the catalog. Pick places that are not already files. Mix interiors and exteriors, and mix time of day.
2. Edit from the anchor above. At most four `image_edit` calls at once. More than that gets rate-limited, and more than eight in one step is rejected.
3. Open each result and check it against the list below before saving. If it fails, edit that plate again from the anchor. Do not save a near miss. Two tries, then keep the better one and note the defect in the reply.
4. If a white or tan mat surrounds the paint, crop to the painted rectangle and scale back to 1280×720 with a high-quality resample. Paper is a near-white edge whose median luminance stays above about 190. Stop the crop where the median falls below that, then step one more pixel into the paint.
5. Save as JPEG RGB quality 92, optimize, `chmod 644`, into `share/`. Overwrite only when redrawing that exact plate.
6. Append the new names to the catalog and run `make -C apps/imageeditor/penciltest thumbs`.

```python
from PIL import Image
im = Image.open(path).convert("RGB")
w, h = im.size
px = im.load()
def row_med(y):
    l = sorted((px[x,y][0]+px[x,y][1]+px[x,y][2]) / 3 for x in range(0, w, 6))
    return l[len(l)//2]
def col_med(x):
    l = sorted((px[x,y][0]+px[x,y][1]+px[x,y][2]) / 3 for y in range(0, h, 6))
    return l[len(l)//2]
if min(row_med(0), row_med(h-1), col_med(0), col_med(w-1)) >= 200:
    top = 0
    while top < h // 4 and row_med(top) > 190: top += 1
    bot = h - 1
    while bot > h * 3 // 4 and row_med(bot) > 190: bot -= 1
    left = 0
    while left < w // 4 and col_med(left) > 190: left += 1
    right = w - 1
    while right > w * 3 // 4 and col_med(right) > 190: right -= 1
    im = im.crop((left + 1, top + 1, right, bot))
if im.size != (1280, 720):
    im = im.resize((1280, 720), Image.Resampling.LANCZOS)
im.save(out, "JPEG", quality=92, optimize=True)
```

## Check before saving

Fail the plate if any of these are true:

- It looks airbrushed, glossy, or like a 3D render. Bark, ground, or foliage has no brush dab.
- A form has a colored halo or a stroke that goes around the outside of the silhouette.
- A paper margin, torn edge, or blank border shows.
- A person, animal, creature, or readable word appears.
- The layout repeats a plate already in `share/`, or repeats a scene from a reference that was not one of our plates.
- The ground is too busy for a character to stand, or the frame is a wide vista with no stage.
- It is not 16:9 after the crop.

## Catalog

| File | Place |
|---|---|
| `forest-glade.jpg` | Sunlit oak clearing. Outdoor anchor. |
| `village-square.jpg` | Timber square and fountain, late afternoon. |
| `castle-courtyard.jpg` | Limestone colonnade and gravel court, morning. |
| `seaside-cove.jpg` | Cliff path above a cove. |
| `cottage-hearth.jpg` | Timber room and hearth. Indoor anchor. |
| `moonlit-garden.jpg` | Formal garden, moon and lanterns. |
| `desert-canyon.jpg` | Red-rock wash at golden hour. |
| `snow-lane.jpg` | Snowy village lane at dusk. |
| `jungle-pool.jpg` | Waterfall pool under broad leaves. |
| `harbor-dusk.jpg` | Lantern pier at dusk. |
| `wheat-field.jpg` | Dirt track through wheat, late afternoon. |
| `autumn-lane.jpg` | Maple lane and a stone wall. |
| `bamboo-path.jpg` | Stone path through misty bamboo. |
| `library.jpg` | High library room, window light. |
| `train-platform.jpg` | Country platform and empty tracks, morning. |
| `rooftop-sunset.jpg` | City roof, water tower, sunset. |
| `throne-hall.jpg` | Columned hall and an empty dais. |
| `cypress-swamp.jpg` | Boardwalk through a dusk swamp. |
| `alpine-meadow.jpg` | High meadow and a path, snow peaks. |
| `rainy-lane.jpg` | Wet cobbled lane, warm windows. |
| `lighthouse-point.jpg` | Lighthouse on a rocky headland. |
| `apple-orchard.jpg` | Apple rows and a grass path. |
| `cave-mouth.jpg` | Cave floor looking out on a valley. |
| `farmyard.jpg` | Red barn and a dirt yard, morning. |
| `greenhouse.jpg` | Glass house and a brick path. |
| `attic.jpg` | Sloping attic and a round window. |
| `savanna.jpg` | Acacia grass and an open stretch of ground. |
| `frozen-pond.jpg` | Snow bank and a frozen pond. |
| `windmill-hill.jpg` | Path up to a wooden windmill. |
| `vineyard.jpg` | Vine terrace and a stone path. |
| `orbital-dock.jpg` | Orbital gantries beside a banded planet. |
| `neon-street.jpg` | Rainy neon street, blank shopfronts. |
| `starship-bridge.jpg` | Starship bridge and a viewport. |
| `mars-outpost.jpg` | Dome outpost on red dunes. |
| `dragon-hoard.jpg` | Sleeping dragon on a gold hoard. The one plate with a creature. |
| `alien-garden.jpg` | Alien garden and a dirt path. |
| `lunar-plain.jpg` | Lunar habitat, Earth, and footprints. |
| `saloon.jpg` | Empty saloon, bar and piano. |
| `western-street.jpg` | Western main street, blank signs. |
| `mesa-trail.jpg` | Mesa trail and wagon ruts. |
| `pirate-deck.jpg` | Ship deck and an empty rail. |
| `engine-room.jpg` | Engine room and a glowing core. |
| `canyon-depot.jpg` | Canyon rail depot and a water tower. |
| `ghost-town.jpg` | Abandoned western street. |
| `egyptian-hall.jpg` | Columned hall, painted glyphs. |
| `crystal-cavern.jpg` | Crystal cavern and a stone floor. |
| `volcano-ledge.jpg` | Lava field and a rocky ledge. |
| `cherry-bridge.jpg` | Blossom bridge over a stream. |
| `mushroom-grove.jpg` | Giant mushroom grove and a path. |
| `observatory.jpg` | Dome, telescope, and an empty floor. |
| `airship-deck.jpg` | Airship deck above the clouds. |
| `victorian-lab.jpg` | Laboratory bench and a wood floor. |
| `mine-tunnel.jpg` | Timbered mine and a lantern. |
| `carnival-dusk.jpg` | Carnival midway at dusk. |
| `roman-atrium.jpg` | Roman atrium and a pool. |
| `pueblo-cliff.jpg` | Cliff dwelling and a terrace. |
| `potion-room.jpg` | Potion workroom and a stone floor. |
| `floating-isles.jpg` | Floating isles and a rope bridge. |
| `ice-cave.jpg` | Ice cave and a frozen floor. |
| `fog-bridge.jpg` | Stone bridge into morning fog. |
