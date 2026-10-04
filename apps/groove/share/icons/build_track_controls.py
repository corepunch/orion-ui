"""Create inactive/active toggle rows from one ImageGen mute/solo strip."""
from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parent
source = Image.open(ROOT / "track-controls-source.png").convert("RGBA")
tile = 64
atlas = Image.new("RGBA", (tile * 2, tile * 2))
for index in range(2):
  art = source.crop((round(index * source.width / 2), 0,
                     round((index + 1) * source.width / 2), source.height))
  bounds = art.getchannel("A").point(lambda a: 255 if a > 16 else 0).getbbox()
  if not bounds:
    raise ValueError(f"Empty icon cell {index}")
  art = art.crop(bounds)
  art.thumbnail((52, 52), Image.Resampling.LANCZOS)
  x, y = tile * index + (tile - art.width) // 2, (tile - art.height) // 2
  atlas.alpha_composite(art, (x, y + tile))
  alpha = art.getchannel("A")
  inactive = ImageEnhance.Color(art).enhance(0)
  inactive = ImageEnhance.Brightness(inactive).enhance(0.75)
  inactive.putalpha(alpha)
  atlas.alpha_composite(inactive, (x, y))
atlas.save(ROOT / "track-controls.png")
