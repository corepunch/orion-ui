float srgb_to_linear(float x) {
  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

// Signed distance to a rounded box centered at the origin.
float roundedBoxSDF(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

vec4 frag() {
  vec4 outColor;
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
