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

static void pencil_path_release(canvas_doc_t *doc) {
  free(doc->pencil_stroke.samples);
  free(doc->pencil_stroke.coverage);
  memset(&doc->pencil_stroke, 0, sizeof(doc->pencil_stroke));
}

typedef struct { float x, y; } pencil_point_t;

static pencil_point_t pencil_bezier_point(pencil_point_t start, pencil_point_t control,
                                          pencil_point_t end, float t) {
  float u = 1.0f - t;
  return (pencil_point_t){u * u * start.x + 2.0f * u * t * control.x + t * t * end.x,
                          u * u * start.y + 2.0f * u * t * control.y + t * t * end.y};
}

static int pencil_curve_steps(pencil_point_t start, pencil_point_t control, pencil_point_t end) {
  float length = hypotf(control.x - start.x, control.y - start.y) +
                 hypotf(end.x - control.x, end.y - control.y);
  return MAX(1, (int)ceilf(2.0f * length));
}

static float pencil_curve_length(pencil_point_t start, pencil_point_t control, pencil_point_t end) {
  int steps = pencil_curve_steps(start, control, end);
  float length = 0.0f;
  pencil_point_t previous = start;
  for (int i = 1; i <= steps; i++) {
    pencil_point_t point = pencil_bezier_point(start, control, end, (float)i / steps);
    length += hypotf(point.x - previous.x, point.y - previous.y);
    previous = point;
  }
  return length;
}

static bool pencil_path_segment(canvas_doc_t *doc, int i, pencil_point_t *start,
                                pencil_point_t *control, pencil_point_t *end,
                                float *start_radius, float *end_radius) {
  if (i < 1 || i >= doc->pencil_stroke.count) return false;
  ipoint16_t a = doc->pencil_stroke.samples[i - 1].point;
  ipoint16_t b = doc->pencil_stroke.samples[i].point;
  if (i == 1) {
    *start = (pencil_point_t){a.x, a.y};
    *control = *start;
  } else {
    ipoint16_t before = doc->pencil_stroke.samples[i - 2].point;
    *start = (pencil_point_t){(before.x + a.x) * 0.5f, (before.y + a.y) * 0.5f};
    *control = (pencil_point_t){a.x, a.y};
  }
  *end = (pencil_point_t){(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
  *start_radius = doc->pencil_stroke.samples[i - 1].radius;
  *end_radius = doc->pencil_stroke.samples[i].radius;
  return true;
}

static bool pencil_path_tail(canvas_doc_t *doc, pencil_point_t *start,
                             pencil_point_t *control, pencil_point_t *end,
                             float *start_radius, float *end_radius) {
  int count = doc->pencil_stroke.count;
  if (count < 2) return false;
  ipoint16_t a = doc->pencil_stroke.samples[count - 2].point;
  ipoint16_t b = doc->pencil_stroke.samples[count - 1].point;
  *start = (pencil_point_t){(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
  *control = *end = (pencil_point_t){b.x, b.y};
  *start_radius = doc->pencil_stroke.samples[count - 2].radius;
  *end_radius = doc->pencil_stroke.samples[count - 1].radius;
  return true;
}

static bool pencil_path_append(canvas_doc_t *doc, ipoint16_t point, float radius) {
  int count = doc->pencil_stroke.count;
  if (count && doc->pencil_stroke.samples[count - 1].point.x == point.x &&
      doc->pencil_stroke.samples[count - 1].point.y == point.y) {
    // Deposited geometry stays fixed; a radius change applies to the next segment.
    return true;
  }
  if (count == doc->pencil_stroke.capacity) {
    int capacity = doc->pencil_stroke.capacity ? doc->pencil_stroke.capacity * 2 : 16;
    void *samples = realloc(doc->pencil_stroke.samples, (size_t)capacity * sizeof(*doc->pencil_stroke.samples));
    if (!samples) {
      IE_TRACE("pencil path allocation failed doc=%p count=%d", (void *)doc, count);
      return false;
    }
    doc->pencil_stroke.samples = samples;
    doc->pencil_stroke.capacity = capacity;
  }
  doc->pencil_stroke.samples[count].point = point;
  doc->pencil_stroke.samples[count].radius = radius;
  doc->pencil_stroke.samples[count].distance = doc->pencil_stroke.length;
  doc->pencil_stroke.count++;
  if (doc->pencil_stroke.count > 1) {
    pencil_point_t start, control, end;
    float start_radius, end_radius;
    pencil_path_segment(doc, doc->pencil_stroke.count - 1, &start, &control, &end,
                        &start_radius, &end_radius);
    doc->pencil_stroke.length += pencil_curve_length(start, control, end);
    doc->pencil_stroke.samples[count].distance = doc->pencil_stroke.length;
  }
  doc->pencil_stroke.max_radius = MAX(doc->pencil_stroke.max_radius, radius);
  return true;
}

static float pencil_profile(float distance, float total) {
  if (total <= 0.0f) return 0;
  float fade = MAX(1.0f, IE_PENCIL_FADE_LENGTH * (float)MAX(1, g_bw_retina_scale));
  float t = MIN(distance / fade, (total - distance) / fade);
  t = CLAMP(t, 0.0f, 1.0f);
  return (float)IE_PENCIL_MAX_OPACITY * t;
}

static int pencil_grain(int x, int y) {
  uint32_t hash = (uint32_t)x * 0x9e3779b1u ^ (uint32_t)y * 0x85ebca77u;
  hash ^= hash >> 16; hash *= 0x7feb352du; hash ^= hash >> 15;
  return (int)(hash % (IE_PENCIL_GRAIN_VARIATION + 1));
}

static void pencil_stamp(canvas_doc_t *doc, float cx, float cy, float along,
                         float total, float previous_total, float tangent_x, float tangent_y,
                         float radius, bool tap) {
  float scale = (float)MAX(1, g_bw_retina_scale);
  float r = radius * scale;
  if (r < 0.5f) r = 0.5f;
  float inner = r * 0.55f, outer = r + 0.85f;
  // The center-line integral of the radial kernel is inner + outer.
  float flow = tap ? 1.0f : IE_PENCIL_DAB_SPACING / (inner + outer);
  int extent = (int)ceilf(outer);
  int x0 = MAX(0, (int)floorf(cx) - extent), x1 = MIN(doc->canvas_w - 1, (int)ceilf(cx) + extent);
  int y0 = MAX(0, (int)floorf(cy) - extent), y1 = MIN(doc->canvas_h - 1, (int)ceilf(cy) + extent);
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
    if (!canvas_in_selection(doc, x, y)) continue;
    float dx = x - cx, dy = y - cy, radial_distance = hypotf(dx, dy);
    if (radial_distance >= outer) continue;
    float radial = radial_distance <= inner ? 1.0f : (outer - radial_distance) / (outer - inner);
    float pixel_along = CLAMP(along + dx * tangent_x + dy * tangent_y, 0.0f, total);
    int grain = pencil_grain(x, y);
    float grain_factor = (255.0f - IE_PENCIL_GRAIN_VARIATION + grain) / 255.0f;
    float previous = along < previous_total ? pencil_profile(pixel_along, previous_total) : 0.0f;
    float alpha = (pencil_profile(pixel_along, total) - previous) * radial * grain_factor * flow;
    if (alpha <= 0.0f) continue;
    size_t at = (size_t)y * doc->canvas_w + x;
    doc->pencil_stroke.coverage[at] = fminf(255.0f, doc->pencil_stroke.coverage[at] + alpha);
    uint8_t value = (uint8_t)lroundf(doc->pencil_stroke.coverage[at]);
    if (value != doc->pixels[at]) {
      doc->pixels[at] = value;
      canvas_mark_dirty_pixel(doc, x, y);
      doc->modified = true;
    }
  }
}

static void pencil_curve_render(canvas_doc_t *doc, pencil_point_t start, pencil_point_t control,
                                pencil_point_t end, float start_radius, float end_radius,
                                float *along, float from_distance, float total,
                                float previous_total) {
  int steps = pencil_curve_steps(start, control, end);
  pencil_point_t previous = start;
  for (int i = 1; i <= steps; i++) {
    float t0 = (float)(i - 1) / steps, t1 = (float)i / steps;
    pencil_point_t point = pencil_bezier_point(start, control, end, t1);
    float dx = point.x - previous.x, dy = point.y - previous.y;
    float length = hypotf(dx, dy);
    if (length > 0.0f && *along + length >= from_distance) {
      int first = (int)ceilf(MAX(*along, from_distance) / IE_PENCIL_DAB_SPACING);
      int limit = (int)ceilf((*along + length) / IE_PENCIL_DAB_SPACING);
      for (int j = first; j < limit; j++) {
        float distance = j * IE_PENCIL_DAB_SPACING;
        float t = t0 + (t1 - t0) * ((distance - *along) / length);
        float tx = 2.0f * ((1.0f - t) * (control.x - start.x) + t * (end.x - control.x));
        float ty = 2.0f * ((1.0f - t) * (control.y - start.y) + t * (end.y - control.y));
        float tangent_length = hypotf(tx, ty);
        if (tangent_length <= 0.0f) { tx = dx / length; ty = dy / length; }
        else { tx /= tangent_length; ty /= tangent_length; }
        pencil_point_t dab = pencil_bezier_point(start, control, end, t);
        float radius = start_radius + (end_radius - start_radius) * t;
        pencil_stamp(doc, dab.x, dab.y, distance, total, previous_total,
                     tx, ty, radius, false);
      }
    }
    *along += length;
    previous = point;
  }
}

static void pencil_path_render(canvas_doc_t *doc, float previous_total, bool include_tail) {
  int count = doc->pencil_stroke.count;
  if (count < 2) return;
  float total = doc->pencil_stroke.length;
  float tail = IE_PENCIL_FADE_LENGTH * stroke_backing_scale() +
               doc->pencil_stroke.max_radius * stroke_backing_scale() + 2.0f;
  float from_distance = MAX(0.0f, previous_total - tail);
  float along = 0.0f;
  for (int i = 1; i < count; i++) {
    if (doc->pencil_stroke.samples[i].distance < from_distance) {
      along = doc->pencil_stroke.samples[i].distance;
      continue;
    }
    pencil_point_t start, control, end;
    float start_radius, end_radius;
    if (pencil_path_segment(doc, i, &start, &control, &end, &start_radius, &end_radius))
      pencil_curve_render(doc, start, control, end, start_radius, end_radius,
                          &along, from_distance, total, previous_total);
  }
  if (include_tail) {
    pencil_point_t start, control, end;
    float start_radius, end_radius;
    if (pencil_path_tail(doc, &start, &control, &end, &start_radius, &end_radius))
      pencil_curve_render(doc, start, control, end, start_radius, end_radius,
                          &along, from_distance, total, previous_total);
  }
}

static bool pencil_path_begin(canvas_doc_t *doc, ipoint16_t point, float radius) {
  pencil_path_release(doc);
  size_t area = (size_t)doc->canvas_w * doc->canvas_h;
  doc->pencil_stroke.coverage = malloc(area * sizeof(*doc->pencil_stroke.coverage));
  if (!doc->pencil_stroke.coverage || !pencil_path_append(doc, point, radius)) {
    pencil_path_release(doc);
    IE_TRACE("pencil stroke allocation failed doc=%p", (void *)doc);
    return false;
  }
  for (size_t i = 0; i < area; i++) doc->pencil_stroke.coverage[i] = doc->pixels[i];
  doc->pencil_stroke.active = true;
  doc->stroke.active = true;
  doc->stroke.soft = true;
  doc->stroke.radius = radius;
  doc->stroke.color = pencil_configured_color();
  IE_TRACE("pencil stroke begin doc=%p win=%p at=(%d,%d) radius=%.2f opacity=%d",
           (void *)doc, (void *)doc->canvas_win, point.x, point.y, radius, IE_PENCIL_MAX_OPACITY);
  return true;
}

static void pencil_path_drag(canvas_doc_t *doc, ipoint16_t point) {
  float previous_length = doc->pencil_stroke.length;
  if (!pencil_path_append(doc, point, doc->stroke.radius)) return;
  pencil_path_render(doc, previous_length, false);
}

static void pencil_path_end(canvas_doc_t *doc, ipoint16_t point) {
  float previous_length = doc->pencil_stroke.length;
  pencil_path_append(doc, point, doc->stroke.radius);
  if (doc->pencil_stroke.count > 1) {
    pencil_point_t start, control, end;
    float start_radius, end_radius;
    if (pencil_path_tail(doc, &start, &control, &end, &start_radius, &end_radius))
      doc->pencil_stroke.length += pencil_curve_length(start, control, end);
  }
  pencil_path_render(doc, previous_length, true);
  if (doc->pencil_stroke.count == 1) {
    float fade = MAX(1.0f, IE_PENCIL_FADE_LENGTH * stroke_backing_scale());
    pencil_stamp(doc, point.x, point.y, fade, fade * 2.0f, 0.0f, 0.0f, 0.0f, doc->stroke.radius, true);
  }
  IE_TRACE("pencil stroke end doc=%p win=%p at=(%d,%d) samples=%d length=%.1f",
           (void *)doc, (void *)doc->canvas_win, point.x, point.y,
           doc->pencil_stroke.count, doc->pencil_stroke.length);
  doc->stroke.active = false;
  pencil_path_release(doc);
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
  if (pencil_has_layers(doc) && doc->layer.active == IE_LAYER_PENCIL && COLOR_A(color) > 0 &&
      pencil_path_begin(doc, point, radius)) return;
  stroke_stamp(doc, point, radius);
}

void canvas_stroke_set_radius(canvas_doc_t *doc, float radius) {
  if (!doc || !doc->stroke.active) return;
  if (radius < 0.0f) radius = 0.0f;
  doc->stroke.radius = stroke_quantize_radius(radius);
}

void canvas_stroke_drag(canvas_doc_t *doc, ipoint16_t point) {
  if (!doc || !doc->stroke.active) return;
  if (doc->pencil_stroke.active) {
    pencil_path_drag(doc, point);
    doc->stroke.sample = doc->last = point;
    return;
  }
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
  if (doc->pencil_stroke.active) {
    pencil_path_end(doc, point);
    return;
  }
  canvas_stroke_drag(doc, point);
  stroke_curve(doc, point.x, point.y, point.x, point.y, doc->stroke.radius);
  doc->stroke.active = false;
}

void canvas_stroke_cancel(canvas_doc_t *doc) {
  if (!doc) return;
  if (doc->pencil_stroke.active) pencil_path_release(doc);
  if (!doc->stroke.active) return;
  doc->stroke.active = false;
}
