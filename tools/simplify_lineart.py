#!/usr/bin/env python3
"""Stroke-aware black/white cleanup. Requires NumPy and opencv-python-headless."""
import argparse
from pathlib import Path
import sys
import cv2
import numpy as np
from lineart_strokes import simplify_strokes, motion_alignment, temporal_support


def rectangle(value):
    try:
        x, y, w, h = map(int, value.split(','))
        if min(x, y) < 0 or min(w, h) <= 0:
            raise ValueError()
        return x, y, w, h
    except ValueError:
        raise argparse.ArgumentTypeError('Region must be X,Y,W,H with nonnegative origin and positive size')


def region_mask(shape, regions):
    if not regions:
        return None
    mask = np.zeros(shape, np.uint8)
    for x, y, w, h in regions:
        if x + w > shape[1] or y + h > shape[0]:
            raise ValueError(f'Region {x},{y},{w},{h} outside image {shape[1]}x{shape[0]}')
        mask[y:y+h, x:x+w] = 1
    return mask


class FrameFilter:
    def __init__(self, args):
        self.args = args
        self.previous_gray = self.previous_ink = None
        self.protect = None

    def process(self, gray):
        args = self.args
        if self.previous_gray is None or self.previous_gray.shape != gray.shape:
            self.protect = region_mask(gray.shape, args.protect_region)
            alignment = None
        else:
            tracking = not args.fixed_protection and self.protect is not None
            alignment = motion_alignment(gray, self.previous_gray) if args.temporal or tracking else None
            if tracking and alignment is not None:
                map_x, map_y, _ = alignment
                if self.protect is not None:
                    self.protect = cv2.remap(self.protect, map_x, map_y, cv2.INTER_NEAREST, borderValue=0)
        support = temporal_support(gray, self.previous_gray, self.previous_ink, alignment) if args.temporal and alignment is not None else None
        drawing = cv2.adaptiveThreshold(gray, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY,
                                        args.outline_window, args.outline_threshold) if args.outlines else gray
        ink, _ = simplify_strokes(drawing, args.threshold, args.min_width, args.min_length,
                                 protect_mask=self.protect, support=support)
        self.previous_gray, self.previous_ink = gray, ink
        return ink


def add_regions(parser):
    parser.add_argument('--protect-region', action='append', default=[], type=rectangle, metavar='X,Y,W,H', help='Preserve original facial or other important ink; repeat as needed')
    parser.add_argument('--fixed-protection', action='store_true', help='Keep region masks fixed instead of following motion')
    parser.add_argument('--temporal', action='store_true', help='Motion-aligned selection hysteresis for a frame sequence')


def add_outlines(parser):
    parser.add_argument('--outlines', action='store_true', help='Extract dark contours from filled artwork')
    parser.add_argument('--outline-threshold', type=int, default=10, help='Darkness below local Gaussian mean; higher keeps less ink')
    parser.add_argument('--outline-window', type=int, default=15, help='Odd local Gaussian window in pixels, 3..255')


def validate(parser, args):
    if not 0 <= args.threshold <= 255 or not 1 <= args.min_width <= 63 or not 0 <= args.min_length <= 1000000:
        parser.error('Threshold must be 0..255, width 1..63, length 0..1000000')
    if not 0 <= args.outline_threshold <= 255 or not 3 <= args.outline_window <= 255 or args.outline_window % 2 != 1:
        parser.error('Outline threshold must be 0..255 and window must be odd, 3..255')


def video_worker(argv):
    parser = argparse.ArgumentParser()
    for name, value_type in [('width', int), ('height', int), ('threshold', int), ('min_width', float),
                             ('min_length', float)]:
        parser.add_argument(name, type=value_type)
    add_regions(parser)
    add_outlines(parser)
    args = parser.parse_args(argv)
    validate(parser, args)
    if not 1 <= args.width <= 16384 or not 1 <= args.height <= 16384 or args.width * args.height > 16000000:
        parser.error('Stroke filter dimensions must be 1..16384 with at most 16 million pixels')
    filtering = FrameFilter(args)
    size = args.width * args.height * 3
    while True:
        raw = sys.stdin.buffer.read(size)
        if not raw:
            break
        if len(raw) != size:
            raise ValueError('Truncated RGB frame in stroke-filter stream')
        rgb = np.frombuffer(raw, np.uint8).reshape(args.height, args.width, 3).astype(np.uint16)
        gray = ((77*rgb[:, :, 0] + 150*rgb[:, :, 1] + 29*rgb[:, :, 2] + 128) >> 8).astype(np.uint8)
        ink = filtering.process(gray)
        output = np.repeat((255-ink*255)[:, :, None], 3, axis=2)
        sys.stdout.buffer.write(output.tobytes())
    sys.stdout.buffer.flush()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--video-worker':
        video_worker(sys.argv[2:])
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='Input image or lexically ordered frame folder')
    parser.add_argument('output', type=Path, help='Output PNG or frame folder')
    parser.add_argument('--threshold', type=int, default=170)
    parser.add_argument('--min-width', type=float, default=3.5)
    parser.add_argument('--min-length', type=float, default=20)
    add_regions(parser)
    add_outlines(parser)
    args = parser.parse_args()
    validate(parser, args)
    if not args.input.exists():
        parser.error(f'Input does not exist: {args.input}')
    if args.input.is_dir():
        sources = [p for p in sorted(args.input.iterdir()) if p.is_file() and p.suffix.lower() in {'.jpg', '.jpeg', '.png', '.bmp', '.webp'}]
        if not sources:
            parser.error('Input folder has no supported images')
    else:
        sources = [args.input]
    filtering = FrameFilter(args)
    for src in sources:
        gray = cv2.imread(str(src), cv2.IMREAD_GRAYSCALE)
        if gray is None:
            raise ValueError(f'Cannot read image: {src}')
        ink = filtering.process(gray)
        dst = args.output / (src.stem + '.png') if args.input.is_dir() else args.output
        if dst.suffix.lower() != '.png':
            parser.error('Output must be PNG to preserve binary ink')
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not cv2.imwrite(str(dst), 255-ink*255, [cv2.IMWRITE_PNG_BILEVEL, 1]):
            raise OSError(f'Cannot write image: {dst}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, cv2.error) as error:
        print(f'lineart: {error}', file=sys.stderr)
        sys.exit(1)
