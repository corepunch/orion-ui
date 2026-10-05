vec3 srgb_to_linear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float box_distance(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

vec4 frag() {
  vec4 outColor;
  vec2 size = params0.xy, p = (tex - 0.5) * size;
  float ring = min(params0.w, max(0.0, min(size.x, size.y) * 0.5 - 1.0));
  vec2 inner = size * 0.5 - vec2(ring);
  float radius = max(0.0, params0.z - ring);
  float outer_d = box_distance(p, size * 0.5, params0.z);
  float inner_d = box_distance(p, inner, radius);
  float outer = 1.0 - smoothstep(-0.75, 0.75, outer_d);
  float face = 1.0 - smoothstep(-0.75, 0.75, inner_d);
  vec2 uv = clamp((tex * size - vec2(ring)) / max(vec2(1.0), size - 2.0 * vec2(ring)), 0.0, 1.0);
  float y = uv.y, shade = clamp(y * 0.8 + uv.x * 0.2, 0.0, 1.0);
  vec3 base = srgb_to_linear(tint.rgb);
  vec3 top = mix(base, vec3(1.0), 0.12 + 0.10 * params1.z);
  vec3 rgb = mix(top, base * mix(0.78, 0.95, params1.z), smoothstep(0.0, 1.0, shade));
  if (params1.x > 0.0) {
    // Light rim around the whole face, brightest along the top edge.
    float rim = 1.0 - smoothstep(params1.x, params1.x + 0.75, abs(inner_d));
    rgb = mix(rgb, vec3(1.0), rim * mix(0.55, 0.22, smoothstep(0.0, 0.5, y)));
  }
  vec3 ring_rgb = mix(base, vec3(1.0), 0.9);
  float coverage = params1.y > 0.5 ? outer : face;
  if (params1.y > 0.5) rgb = mix(ring_rgb, rgb, face);
  float a = coverage * tint.a * col.a;
  outColor = vec4(rgb * col.rgb * a, a);
  return outColor;
}
