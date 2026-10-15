"""Run with Python plus NumPy/OpenCV: python3 tools/test_lineart_strokes.py."""
import unittest
import subprocess
import sys
from pathlib import Path
import cv2
import numpy as np
from lineart_strokes import simplify_strokes, skeletonize, temporal_support


class StrokeTests(unittest.TestCase):
    def test_outline_worker_removes_fills_and_exposes_darkness_threshold(self):
        script = str(Path(__file__).with_name('simplify_lineart.py'))
        gray = np.full((80, 100), 255, np.uint8)
        gray[10:70, 10:90] = 100
        gray[40, 25:75] = 80
        rgb = np.repeat(gray[:, :, None], 3, axis=2).tobytes()
        args = [sys.executable, script, '--video-worker', '100', '80', '170', '1', '0',
                '--outlines', '--outline-window', '15', '--outline-threshold']
        outputs = []
        for threshold in ('10', '25'):
            result = subprocess.run(args + [threshold], input=rgb + rgb, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            frames = np.frombuffer(result.stdout, np.uint8).reshape(2, 80, 100, 3)
            self.assertTrue(np.array_equal(frames[0], frames[1]))
            outputs.append(frames[0, :, :, 0])
        low, high = outputs
        self.assertEqual(low[25, 50], 255, 'Uniform colored interiors must become empty paper')
        self.assertEqual(low[10, 50], 0, 'Outer contour must remain')
        self.assertEqual(low[40, 50], 0, 'Low threshold must retain the faint detail')
        self.assertEqual(high[40, 50], 255, 'Higher threshold must omit the faint detail')
        self.assertFalse(np.any((high == 0) & (low != 0)))
        failed = subprocess.run(args[:-1] + ['--outline-threshold', '256'], input=rgb, capture_output=True)
        self.assertNotEqual(failed.returncode, 0)
        failed = subprocess.run(args + ['10', '--outline-window', '14'], input=rgb, capture_output=True)
        self.assertNotEqual(failed.returncode, 0)

    def test_video_worker_stream_and_errors(self):
        script = str(Path(__file__).with_name('simplify_lineart.py'))
        args = [sys.executable, script, '--video-worker', '80', '60', '170', '3.5', '20',
                '--protect-region', '30,25,15,10', '--fixed-protection']
        gray = np.full((60, 80), 255, np.uint8)
        gray[30, 35:39] = 0
        rgb = np.repeat(gray[:, :, None], 3, axis=2).tobytes()
        result = subprocess.run(args, input=rgb + rgb, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, rgb + rgb)
        failed = subprocess.run(args, input=rgb[:-1], capture_output=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn(b'Truncated RGB frame', failed.stderr)

    def test_binary_subset_and_determinism(self):
        rng = np.random.default_rng(19)
        gray = rng.integers(0, 256, (48, 64), np.uint8)
        a, _ = simplify_strokes(gray)
        b, _ = simplify_strokes(gray)
        self.assertTrue(np.array_equal(a, b))
        self.assertTrue(set(np.unique(a)) <= {0, 1})
        self.assertFalse(np.any((a != 0) & (gray >= 170)))

    def test_connections_loops_and_spurs(self):
        gray = np.full((90, 140), 255, np.uint8)
        cv2.rectangle(gray, (15, 25), (55, 75), 0, 5)
        cv2.rectangle(gray, (85, 25), (125, 75), 0, 5)
        cv2.line(gray, (55, 50), (85, 50), 0, 1)
        cv2.line(gray, (35, 25), (35, 8), 0, 1)
        cv2.line(gray, (65, 12), (80, 12), 0, 1)
        result, _ = simplify_strokes(gray, min_width=2.5, min_length=4)
        self.assertEqual(result[50, 70], 1, 'Thin connector between contours must survive')
        self.assertEqual(result[12, 35], 0, 'Thin terminal spur should be pruned')
        self.assertEqual(result[12, 70], 0, 'Thin isolated stroke should be pruned')
        white = (1 - result).copy()
        filled = white.copy()
        cv2.floodFill(filled, None, (0, 0), 2)
        self.assertEqual(filled[50, 35], 1, 'Left enclosed fill region must remain closed')
        self.assertEqual(filled[50, 105], 1, 'Right enclosed fill region must remain closed')

    def test_empty_solid_and_frame_border(self):
        for value in (0, 255):
            gray = np.full((31, 35), value, np.uint8)
            result, _ = simplify_strokes(gray, min_length=10)
            self.assertTrue(np.all(result == (value == 0)))
        gray = np.full((31, 35), 255, np.uint8)
        gray[0:20, 0] = 0
        result, _ = simplify_strokes(gray)
        self.assertTrue(np.all(result[0:20, 0] == 1))

    def test_optional_isolated_length_and_loop_area(self):
        gray = np.full((60, 80), 255, np.uint8)
        cv2.line(gray, (8, 12), (15, 12), 0, 3)
        cv2.circle(gray, (50, 35), 8, 0, 1)
        base, _ = simplify_strokes(gray, min_width=1)
        short, _ = simplify_strokes(gray, min_width=3, min_length=20)
        no_loop, _ = simplify_strokes(gray, min_width=1, max_detail_area=400)
        self.assertEqual(base[12, 10], 1)
        self.assertEqual(short[12, 10], 0)
        self.assertTrue(np.any(base[24:46, 39:61]))
        self.assertFalse(np.any(no_loop[24:46, 39:61]))

    def test_protected_small_feature_is_not_discarded(self):
        gray = np.full((60, 80), 255, np.uint8)
        cv2.line(gray, (35, 30), (38, 31), 0, 1)
        mask = np.zeros_like(gray)
        mask[25:35, 30:45] = 1
        erased, _ = simplify_strokes(gray, min_width=3.5, min_length=20)
        protected, _ = simplify_strokes(gray, min_width=3.5, min_length=20, protect_mask=mask)
        self.assertFalse(np.any(erased))
        self.assertTrue(np.array_equal(protected[mask != 0], (gray < 170)[mask != 0]))

    def test_skeleton_and_static_temporal_alignment(self):
        gray = np.full((90, 140), 255, np.uint8)
        cv2.rectangle(gray, (20, 20), (110, 70), 0, 5)
        skeleton = skeletonize(gray < 170)
        self.assertFalse(np.any((skeleton != 0) & (gray >= 170)))
        result, _ = simplify_strokes(gray)
        again, _ = simplify_strokes(gray, previous_gray=gray, previous_ink=result)
        self.assertTrue(np.array_equal(result, again))
        aligned, valid = temporal_support(gray, gray, result)
        self.assertTrue(np.all(aligned[valid] == (result[valid] != 0)))
        self.assertIsNone(temporal_support(gray, np.zeros((2, 2), np.uint8), result))


if __name__ == '__main__':
    unittest.main()
