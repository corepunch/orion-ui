"""Normalize the single ImageGen strip and derive matching pressed artwork."""
from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parent
source = Image.open(ROOT / "transport-source.png").convert("RGBA")
tile = 64
atlas = Image.new("RGBA", (tile * 8, tile * 2))
# Use the eight separated silhouettes; generation can shift centers by a few pixels.
alpha = source.getchannel("A")
runs = []
start = None
for x in range(source.width + 1):
  occupied = x < source.width and sum(a > 96 for a in alpha.crop((x, 0, x + 1, source.height)).get_flattened_data()) > 4
  if occupied and start is None:
    start = x
  if not occupied and start is not None:
    if x - start > 8:
      runs.append((start, x))
    start = None
if len(runs) != 8:
  raise ValueError(f"Expected eight icon silhouettes, got {len(runs)}")
for index in range(8):
  left, right = runs[index]
  art = source.crop((max(0, left - 2), 0, min(source.width, right + 2), source.height))
  bounds = art.getchannel("A").point(lambda a: 255 if a > 16 else 0).getbbox()
  if not bounds:
    raise ValueError(f"Empty icon cell {index}")
  art = art.crop(bounds)
  art.thumbnail((52, 52), Image.Resampling.LANCZOS)
  atlas.alpha_composite(art, (tile * index + (tile - art.width) // 2, (tile - art.height) // 2))
  pressed = art.resize((round(art.width * 0.94), round(art.height * 0.94)), Image.Resampling.LANCZOS)
  alpha = pressed.getchannel("A")
  pressed = ImageEnhance.Brightness(pressed).enhance(0.82)
  pressed.putalpha(alpha)
  atlas.alpha_composite(pressed, (tile * index + (tile - pressed.width) // 2,
                                tile + (tile - pressed.height) // 2 + 1))
atlas.save(ROOT / "transport.png")
