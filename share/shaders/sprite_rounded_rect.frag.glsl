float srgb_to_linear(float x) {
  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

// Signed distance to a rounded box centered at the origin.
float roundedBoxSDF(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

float triangleSDF(vec2 p, vec2 a, vec2 b, vec2 c) {
  vec2 e0 = b - a, e1 = c - b, e2 = a - c;
  vec2 v0 = p - a, v1 = p - b, v2 = p - c;
  vec2 q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
  vec2 q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
  vec2 q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
  vec3 signs = vec3(cross2(e0, v0), cross2(e1, v1), cross2(e2, v2));
  bool inside = all(greaterThanEqual(signs, vec3(0.0))) || all(lessThanEqual(signs, vec3(0.0)));
  return sqrt(min(dot(q0, q0), min(dot(q1, q1), dot(q2, q2)))) * (inside ? -1.0 : 1.0);
}

float bubbleSDF(vec2 p) {
  float tail = abs(params0.w), top = params0.w > 0.0 ? tail : 0.0;
  vec2 body = vec2(size.x, size.y - tail);
  float box = roundedBoxSDF(p - vec2(body.x * 0.5, top + body.y * 0.5), body * 0.5, radius);
  float base = params0.w > 0.0 ? tail + 0.5 : body.y - 0.5;
  float tip = params0.w > 0.0 ? 0.0 : size.y;
  float tri = triangleSDF(p, vec2(params1.w - tail, base), vec2(params1.w + tail, base), vec2(params1.w, tip));
  return min(box, tri);
}

vec4 frag() {
  vec4 outColor;
  if (params0.z > 1.5) {
    vec2 p = vec2(tex.x, 1.0 - tex.y) * (size + 2.0 * params0.y) - params0.y;
    float d = bubbleSDF(p);
    float aa = max(0.75, fwidth(d));
    float face = 1.0 - smoothstep(-aa, aa, d);
    float fa = tint.a * face * alpha;
    vec3 fill_rgb = vec3(srgb_to_linear(tint.r), srgb_to_linear(tint.g), srgb_to_linear(tint.b));
    return vec4(fill_rgb * fa, fa);
  }
  if (params0.x > 0.0) {
    vec2 p = (tex - 0.5) * (size + 2.0 * params0.y);
    float d = roundedBoxSDF(p, size * 0.5, radius);
    // Gaussian-tail approximation to a blurred silhouette, evaluated in one pass.
    float x = abs(d) / (params0.x * 1.41421356);
    float t = 1.0 / (1.0 + 0.3275911 * x);
    float tail = (((((1.061405429 * t - 1.453152027) * t) + 1.421413741)
                   * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
    float coverage = 0.5 * tail;
    if (d < 0.0) coverage = 1.0 - coverage;
    vec3 shadow_rgb = vec3(srgb_to_linear(tint.r), srgb_to_linear(tint.g),
                           srgb_to_linear(tint.b));
    float shadow_alpha = tint.a * alpha * coverage;
    outColor = vec4(shadow_rgb * shadow_alpha, shadow_alpha);
    return outColor;
  }
  vec4 src = texture(tex0, tex);
  vec3 tint_linear = vec3(srgb_to_linear(tint.r), srgb_to_linear(tint.g),
                          srgb_to_linear(tint.b));
  float aa = 1.0;
  float stroke = params1.z;
  if (radius > 0.0 || stroke > 0.0) {
    vec2 pixel = tex * size;
    vec2 center = size * 0.5;
    float d = roundedBoxSDF(pixel - center, size * 0.5, max(radius, 0.0));
    float outer = 1.0 - smoothstep(-1.5, 1.5, d);
    if (stroke > 0.0) {
      float inner = 1.0 - smoothstep(-1.5, 1.5, d + stroke);
      aa = max(0.0, outer - inner);
    } else {
      aa = outer;
    }
  }
  if (params0.z > 0.5) {
    vec3 bottom = vec3(srgb_to_linear(edge.r), srgb_to_linear(edge.g), srgb_to_linear(edge.b));
    // Texture coordinates are flipped for the compositor: top is tex.y = 1.
    float y = clamp(1.0 - tex.y, 0.0, 1.0);
    float a = mix(tint.a, edge.a, y) * alpha * aa * col.a;
    outColor = vec4(mix(tint_linear, bottom, y) * col.rgb * a, a);
    return outColor;
  }
  float factor = alpha * aa * col.a * tint.a;
  vec3 straight_rgb = src.rgb * col.rgb * tint_linear;
  if (params1.x < 0.5) straight_rgb *= src.a;
  float src_a = src.a;
  // A plain rectangular edge, clipped by the same rounded silhouette as the fill.
  if (stroke <= 0.0 && params1.y > 0.0 && tex.x * size.x < params1.y) {
    straight_rgb = vec3(srgb_to_linear(edge.r), srgb_to_linear(edge.g), srgb_to_linear(edge.b));
    factor = alpha * aa * col.a * edge.a;
    src_a = 1.0;
  }
  outColor = vec4(straight_rgb * factor, src_a * factor);
  return outColor;
}
