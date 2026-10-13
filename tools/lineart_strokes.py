"""Stroke-aware ink selection with protected regions and optional temporal hysteresis."""
import cv2
import numpy as np


def skeletonize(ink):
    pixels = np.pad((ink != 0).astype(np.uint8), 1)
    while True:
        changed = False
        for step in (0, 1):
            p = pixels[1:-1, 1:-1]
            n = [pixels[:-2, 1:-1], pixels[:-2, 2:], pixels[1:-1, 2:],
                 pixels[2:, 2:], pixels[2:, 1:-1], pixels[2:, :-2],
                 pixels[1:-1, :-2], pixels[:-2, :-2]]
            count = sum(n)
            transitions = sum((a == 0) & (b != 0) for a, b in zip(n, n[1:] + n[:1]))
            a, b, c, d = n[0], n[2], n[4], n[6]
            preserve = (a * b * c == 0) & (b * c * d == 0) if step == 0 else \
                       (a * b * d == 0) & (a * c * d == 0)
            erase = (p != 0) & (count >= 2) & (count <= 6) & (transitions == 1) & preserve
            changed |= bool(np.any(erase))
            p[erase] = 0
        if not changed:
            return pixels[1:-1, 1:-1].copy()


def motion_alignment(gray, previous_gray):
    if previous_gray is None or previous_gray.shape != gray.shape:
        return None
    flow = cv2.calcOpticalFlowFarneback(gray, previous_gray, None, .5, 4, 25, 4, 7, 1.5, 0)
    reverse = cv2.calcOpticalFlowFarneback(previous_gray, gray, None, .5, 4, 25, 4, 7, 1.5, 0)
    yy, xx = np.indices(gray.shape, dtype=np.float32)
    map_x, map_y = xx + flow[:, :, 0], yy + flow[:, :, 1]
    aligned_gray = cv2.remap(previous_gray, map_x, map_y, cv2.INTER_LINEAR, borderValue=255)
    aligned_reverse = cv2.remap(reverse, map_x, map_y, cv2.INTER_LINEAR)
    reliable = (np.linalg.norm(flow + aligned_reverse, axis=2) < 1.5) & \
               (np.abs(gray.astype(np.int16) - aligned_gray.astype(np.int16)) < 35) & \
               (map_x >= 0) & (map_x < gray.shape[1] - 1) & (map_y >= 0) & (map_y < gray.shape[0] - 1)
    return map_x, map_y, reliable


def temporal_support(gray, previous_gray, previous_ink, alignment=None):
    alignment = motion_alignment(gray, previous_gray) if alignment is None else alignment
    if alignment is None:
        return None
    map_x, map_y, reliable = alignment
    aligned_ink = cv2.remap(previous_ink, map_x, map_y, cv2.INTER_NEAREST, borderValue=0)
    return aligned_ink != 0, reliable


def simplify_strokes(gray, threshold=170, min_width=3.5, min_length=20,
                     max_detail_area=0, previous_gray=None, previous_ink=None,
                     hysteresis=.35, protect_mask=None, support=None):
    ink = (gray < threshold).astype(np.uint8)
    if not np.any(ink):
        return ink, {'removed_branches': 0, 'removed_pixels': 0}
    distance = cv2.distanceTransform(np.pad(ink, 1), cv2.DIST_L2, cv2.DIST_MASK_PRECISE)[1:-1, 1:-1]
    skeleton = skeletonize(ink)
    points = [tuple(p) for p in np.argwhere(skeleton)]
    point_set = set(points)
    graph = {}
    for y, x in points:
        neighbors = []
        for dy, dx in ((-1, 0), (0, 1), (1, 0), (0, -1), (-1, -1), (-1, 1), (1, 1), (1, -1)):
            q = (y + dy, x + dx)
            if q not in point_set:
                continue
            if dy and dx and ((y + dy, x) in point_set or (y, x + dx) in point_set):
                continue
            neighbors.append(q)
        graph[y, x] = neighbors
    if support is None and previous_ink is not None:
        support = temporal_support(gray, previous_gray, previous_ink)
    removed, seen = set(), set()
    branches = 0

    def thin(path):
        yy, xx = np.array(path).T
        if protect_mask is not None and np.any(protect_mask[yy, xx]):
            return False
        widths = 2 * distance[yy, xx] - 1
        cutoff = min_width
        if support is not None:
            kept, reliable = support
            valid = reliable[yy, xx]
            if np.mean(valid) >= .6:
                cutoff += -hysteresis if np.mean(kept[yy[valid], xx[valid]]) >= .5 else hysteresis
        return np.mean(widths < cutoff) >= .8

    for p in points:
        if p in seen:
            continue
        pending, component = [p], []
        seen.add(p)
        while pending:
            q = pending.pop()
            component.append(q)
            for neighbor in graph[q]:
                if neighbor not in seen:
                    seen.add(neighbor)
                    pending.append(neighbor)
        length = sum(np.hypot(q[0] - r[0], q[1] - r[1]) for q in component for r in graph[q]) / 2
        endpoints = [q for q in component if len(graph[q]) <= 1]
        if not any(len(graph[q]) > 2 for q in component):
            yy, xx = np.array(component).T
            on_border = np.any((yy == 0) | (xx == 0) | (yy == gray.shape[0] - 1) | (xx == gray.shape[1] - 1))
            small_loop = not endpoints and max_detail_area > 0 and \
                         (np.ptp(yy) + 1) * (np.ptp(xx) + 1) <= max_detail_area
            short = length < min_length and np.max(2 * distance[yy, xx] - 1) < 2 * min_width
            protected = protect_mask is not None and np.any(protect_mask[yy, xx])
            if not on_border and not protected and ((bool(endpoints) and thin(component)) or short or small_loop):
                removed.update(component)
                branches += 1
                continue
        for endpoint in endpoints:
            path, prior, current = [], None, endpoint
            while len(graph[current]) <= 2:
                path.append(current)
                nxt = [q for q in graph[current] if q != prior]
                if not nxt:
                    break
                prior, current = current, nxt[0]
            if len(graph[current]) <= 2 or len(path) < 2:
                continue
            yy, xx = np.array(path).T
            if np.any((yy == 0) | (xx == 0) | (yy == gray.shape[0] - 1) | (xx == gray.shape[1] - 1)):
                continue
            # Never remove junctions, loops, or paths joining two junctions.
            if thin(path):
                removed.update(path)
                branches += 1
    selected = skeleton.copy()
    for y, x in removed:
        selected[y, x] = 0
    if not np.any(selected):
        result = np.zeros_like(ink) if protect_mask is None else ink * (protect_mask != 0)
        return result, {'removed_branches': branches, 'removed_pixels': int(np.sum(ink) - np.sum(result))}
    # Assign original ink pixels to their closest skeleton sample; do not redraw contours.
    _, labels = cv2.distanceTransformWithLabels(1 - skeleton, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
    keep_label = np.zeros(int(labels.max()) + 1, np.uint8)
    keep_label[labels[selected != 0]] = 1
    result = ink * keep_label[labels]
    if protect_mask is not None:
        result[protect_mask != 0] = ink[protect_mask != 0]
    return result, {'removed_branches': branches, 'removed_pixels': int(np.sum(ink) - np.sum(result))}
