# Freehand curves

Pencil, brush and eraser share `canvas_stroke.c`. Each pointer sample is a
quadratic Bézier control point; adjacent sample midpoints are segment endpoints.
The joins share a tangent, rounding the raw polyline's corners and reducing
small zigzags without overshooting the control triangle. Floating-point
midpoints preserve half-pixel precision until rasterization.

The first sample is stamped immediately. While moving, the rendered path trails
the pointer by half the latest sample interval. Release adds its coordinate and
finishes the remaining tail, including strokes with no move events. Cancel
discards the pending tail; the caller owns undo and pixel restoration.

Sampling uses the control-polygon length to bound steps to one pixel. Consecutive
duplicate raster positions are skipped so segment joins do not add extra soft
brush opacity unless the stamp radius changed. Pencil and brush scale that radius
from Apple Pencil altitude as a float: upright (π/2) is 50% of the selected
size, a 45° drawing grip is 100%, and flatter than that continues linearly.
Size 0 is 0.5 logical so 50% can stamp a single backing pixel. Radius snaps to
half a backing pixel and interpolates along each curve segment. Existing stamp
functions retain selection clipping, indexed colors, erasing, Retina sizing and
texture damage tracking.

Quadratic curve definition: [W3C SVG paths](https://www.w3.org/TR/SVG/paths.html#PathDataQuadraticBezierCommands).
An alternative is [centripetal Catmull–Rom](https://www.cemyuksel.com/research/catmullrom_param/),
which interpolates every sample and avoids within-segment cusps. Midpoint
quadratics were chosen here to soften noisy samples instead of passing through
each jitter point. This does not add edge antialiasing to the pixel pencil.
