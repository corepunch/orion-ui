#include "../imageeditor.h"

static void stroke_stamp(canvas_doc_t *doc, ipoint16_t point) {
  if (doc->stroke.soft)
    canvas_draw_scaled_soft_circle(doc, point.x, point.y, doc->stroke.radius, doc->stroke.color);
  else
    canvas_draw_scaled_circle(doc, point.x, point.y, doc->stroke.radius, doc->stroke.color);
  doc->stroke.stamp = point;
}

static void stroke_curve(canvas_doc_t *doc, float cx, float cy, float ex, float ey) {
  float sx = doc->stroke.x, sy = doc->stroke.y;
  float length = hypotf(cx - sx, cy - sy) + hypotf(ex - cx, ey - cy);
  // Twice the control-polygon length bounds each step to at most one pixel.
  int steps = MAX(1, (int)ceilf(2.0f * length));
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps, u = 1.0f - t;
    ipoint16_t point = {
      (int16_t)lroundf(u * u * sx + 2 * u * t * cx + t * t * ex),
      (int16_t)lroundf(u * u * sy + 2 * u * t * cy + t * t * ey)
    };
    if (point.x != doc->stroke.stamp.x || point.y != doc->stroke.stamp.y)
      stroke_stamp(doc, point);
  }
  doc->stroke.x = ex;
  doc->stroke.y = ey;
}

void canvas_stroke_begin(canvas_doc_t *doc, ipoint16_t point, int radius, uint32_t color, bool soft) {
  if (!doc) return;
  doc->stroke.active = true;
  doc->stroke.soft = soft;
  doc->stroke.radius = radius;
  doc->stroke.color = color;
  doc->stroke.sample = doc->last = point;
  doc->stroke.x = point.x;
  doc->stroke.y = point.y;
  stroke_stamp(doc, point);
  // IE_TRACE("stroke begin win=%p doc=%p at=(%d,%d) radius=%d soft=%d",
  //          (void *)doc->canvas_win, (void *)doc, point.x, point.y, radius, soft);
}

void canvas_stroke_drag(canvas_doc_t *doc, ipoint16_t point) {
  if (!doc || !doc->stroke.active) return;
  ipoint16_t prev = doc->stroke.sample;
  if (point.x == prev.x && point.y == prev.y) return;
  // Midpoint joins share a tangent; raw samples control the rounded turns.
  stroke_curve(doc, prev.x, prev.y, (prev.x + point.x) * 0.5f, (prev.y + point.y) * 0.5f);
  doc->stroke.sample = doc->last = point;
  // IE_TRACE("stroke drag win=%p doc=%p at=(%d,%d)",
  //          (void *)doc->canvas_win, (void *)doc, point.x, point.y);
}

void canvas_stroke_end(canvas_doc_t *doc, ipoint16_t point) {
  if (!doc || !doc->stroke.active) return;
  canvas_stroke_drag(doc, point);
  stroke_curve(doc, point.x, point.y, point.x, point.y);
  doc->stroke.active = false;
  // IE_TRACE("stroke end win=%p doc=%p at=(%d,%d)",
  //          (void *)doc->canvas_win, (void *)doc, point.x, point.y);
}

void canvas_stroke_cancel(canvas_doc_t *doc) {
  if (!doc || !doc->stroke.active) return;
  doc->stroke.active = false;
  // IE_TRACE("stroke cancel win=%p doc=%p", (void *)doc->canvas_win, (void *)doc);
}
