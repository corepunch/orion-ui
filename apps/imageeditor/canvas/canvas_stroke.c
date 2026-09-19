#include "../imageeditor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

static float stroke_backing_scale(void) {
  return (float)MAX(1, g_bw_retina_scale);
}

static float stroke_backing_radius(float logical) {
  if (logical <= 0.0f) return 0.0f;
  float backing = logical * stroke_backing_scale();
  return fmaxf(0.5f, roundf(backing * 2.0f) * 0.5f);
}

static float stroke_quantize_radius(float logical) {
  if (logical <= 0.0f) return 0.0f;
  return stroke_backing_radius(logical) / stroke_backing_scale();
}

static int stroke_backing_steps(float logical) {
  return (int)lroundf(stroke_backing_radius(logical) * 2.0f);
}

float canvas_tilt_radius(float base, float altitude) {
  float base_r = base <= 0.0f ? 0.5f : base;
  float a = altitude;
  if (a < 0.0f) a = 0.0f;
  if (a > (float)M_PI_2) a = (float)M_PI_2;
  // Linear in altitude: upright (π/2) → 50%, natural 45° grip → 100%,
  // flat on the glass → 150%.
  float t = ((float)M_PI_2 - a) / ((float)M_PI_2 - IE_PENCIL_NATURAL_ALTITUDE);
  float scale = IE_PENCIL_UPRIGHT_SCALE +
                (IE_PENCIL_NATURAL_SCALE - IE_PENCIL_UPRIGHT_SCALE) * t;
  if (scale < IE_PENCIL_UPRIGHT_SCALE) scale = IE_PENCIL_UPRIGHT_SCALE;
  return stroke_quantize_radius(base_r * scale);
}

float canvas_pointer_radius(float base) {
  ax_pointer_t p = ui_get_pointer();
  if (!(p.flags & AX_POINTER_STYLUS)) return base < 0.0f ? 0.0f : base;
  return canvas_tilt_radius(base, p.altitude);
}

static void stroke_stamp(canvas_doc_t *doc, ipoint16_t point, float radius) {
  if (radius < 0.0f) radius = 0.0f;
  radius = stroke_quantize_radius(radius);
  if (doc->stroke.soft)
    canvas_draw_scaled_soft_circle(doc, point.x, point.y, radius, doc->stroke.color);
  else
    canvas_draw_scaled_circle(doc, point.x, point.y, radius, doc->stroke.color);
  doc->stroke.stamp = point;
  doc->stroke.stamp_radius = radius;
}

static void stroke_curve(canvas_doc_t *doc, float cx, float cy, float ex, float ey, float r1) {
  float sx = doc->stroke.x, sy = doc->stroke.y;
  float r0 = doc->stroke.stamp_radius;
  r1 = stroke_quantize_radius(r1);
  float length = hypotf(cx - sx, cy - sy) + hypotf(ex - cx, ey - cy);
  // Twice the control-polygon length bounds each step to at most one pixel.
  int steps = MAX(1, (int)ceilf(2.0f * length));
  int r0_steps = stroke_backing_steps(r0), r1_steps = stroke_backing_steps(r1);
  int radius_span = abs(r1_steps - r0_steps);
  if (radius_span > steps) steps = radius_span;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps, u = 1.0f - t;
    ipoint16_t point = {
      (int16_t)lroundf(u * u * sx + 2 * u * t * cx + t * t * ex),
      (int16_t)lroundf(u * u * sy + 2 * u * t * cy + t * t * ey)
    };
    float radius = stroke_quantize_radius(r0 + (r1 - r0) * t);
    if (point.x != doc->stroke.stamp.x || point.y != doc->stroke.stamp.y ||
        stroke_backing_steps(radius) != stroke_backing_steps(doc->stroke.stamp_radius))
      stroke_stamp(doc, point, radius);
  }
  doc->stroke.x = ex;
  doc->stroke.y = ey;
  doc->stroke.radius = r1;
}

void canvas_stroke_begin(canvas_doc_t *doc, ipoint16_t point, float radius, uint32_t color, bool soft) {
  if (!doc) return;
  if (radius < 0.0f) radius = 0.0f;
  radius = stroke_quantize_radius(radius);
  doc->stroke.active = true;
  doc->stroke.soft = soft;
  doc->stroke.radius = radius;
  doc->stroke.color = color;
  doc->stroke.sample = doc->last = point;
  doc->stroke.x = point.x;
  doc->stroke.y = point.y;
  stroke_stamp(doc, point, radius);
}

void canvas_stroke_set_radius(canvas_doc_t *doc, float radius) {
  if (!doc || !doc->stroke.active) return;
  if (radius < 0.0f) radius = 0.0f;
  doc->stroke.radius = stroke_quantize_radius(radius);
}

void canvas_stroke_drag(canvas_doc_t *doc, ipoint16_t point) {
  if (!doc || !doc->stroke.active) return;
  ipoint16_t prev = doc->stroke.sample;
  float radius = doc->stroke.radius;
  if (point.x == prev.x && point.y == prev.y) {
    if (stroke_backing_steps(radius) != stroke_backing_steps(doc->stroke.stamp_radius))
      stroke_stamp(doc, point, radius);
    return;
  }
  // Midpoint joins share a tangent; raw samples control the rounded turns.
  stroke_curve(doc, prev.x, prev.y, (prev.x + point.x) * 0.5f, (prev.y + point.y) * 0.5f, radius);
  doc->stroke.sample = doc->last = point;
}

void canvas_stroke_end(canvas_doc_t *doc, ipoint16_t point) {
  if (!doc || !doc->stroke.active) return;
  canvas_stroke_drag(doc, point);
  stroke_curve(doc, point.x, point.y, point.x, point.y, doc->stroke.radius);
  doc->stroke.active = false;
}

void canvas_stroke_cancel(canvas_doc_t *doc) {
  if (!doc || !doc->stroke.active) return;
  doc->stroke.active = false;
}
