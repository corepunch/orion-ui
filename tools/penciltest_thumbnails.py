#!/usr/bin/env python3
"""Square stock-background thumbnails for Pencil Test's background browser.

Each plate in the stock folder gets a centered square crop, area-averaged in
linear light down to SIZE x SIZE, written as JPEG to <stock>/thumbs/<name>.jpg.

    python3 tools/penciltest_thumbnails.py apps/imageeditor/penciltest/share
"""
import argparse
import pathlib

import numpy as np
from PIL import Image


def to_linear(srgb):
    c = srgb / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(linear):
    c = np.clip(linear, 0.0, 1.0)
    c = np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)
    return np.clip(np.round(c * 255.0), 0, 255).astype(np.uint8)


def thumbnail(path, size):
    image = Image.open(path).convert('RGB')
    side = min(image.size)
    left, top = (image.width - side) // 2, (image.height - side) // 2
    square = image.crop((left, top, left + side, top + side))
    linear = to_linear(np.asarray(square, dtype=np.float64))
    channels = [Image.fromarray(linear[:, :, c].astype(np.float32), 'F').resize((size, size), Image.BOX)
                for c in range(3)]
    return Image.fromarray(to_srgb(np.stack([np.asarray(c) for c in channels], axis=2)), 'RGB')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('stock', type=pathlib.Path)
    parser.add_argument('--size', type=int, default=128)
    parser.add_argument('--quality', type=int, default=85)
    args = parser.parse_args()
    out = args.stock / 'thumbs'
    out.mkdir(exist_ok=True)
    plates = sorted(args.stock.glob('*.jpg'))
    for plate in plates:
        thumbnail(plate, args.size).save(out / plate.name, quality=args.quality, optimize=True)
        (out / plate.name).chmod(0o644)
    print(f'{len(plates)} thumbnails in {out}')


if __name__ == '__main__':
    main()
