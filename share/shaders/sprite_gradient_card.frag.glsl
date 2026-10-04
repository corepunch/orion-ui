#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;
uniform vec4 tint;
uniform vec4 params0; // pixel size, corner radius, reserved ring width
uniform vec4 params1; // highlight width, selected, hover

vec3 srgb_to_linear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float box_distance(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
  vec2 size = params0.xy, p = (tex - 0.5) * size;
  float ring = min(params0.w, max(0.0, min(size.x, size.y) * 0.5 - 1.0));
  vec2 inner = size * 0.5 - vec2(ring);
  float radius = max(0.0, params0.z - ring);
  float outer_d = box_distance(p, size * 0.5, params0.z);
  float inner_d = box_distance(p, inner, radius);
  float outer = 1.0 - smoothstep(-0.75, 0.75, outer_d);
  float face = 1.0 - smoothstep(-0.75, 0.75, inner_d);
  float y = clamp((tex.y * size.y - ring) / max(1.0, size.y - 2.0 * ring), 0.0, 1.0);
  vec3 base = srgb_to_linear(tint.rgb);
  vec3 rgb = mix(mix(base, vec3(1.0), 0.04 + 0.05 * params1.z), base * 0.72, y);
  if (params1.x > 0.0) {
    float rim = 1.0 - smoothstep(params1.x, params1.x + 0.75, abs(inner_d));
    rgb = mix(rgb, vec3(1.0), rim * (1.0 - smoothstep(0.0, 0.35, y)) * 0.6);
  }
  vec3 ring_rgb = mix(base, vec3(1.0), 0.82);
  float coverage = params1.y > 0.5 ? outer : face;
  if (params1.y > 0.5) rgb = mix(ring_rgb, rgb, face);
  float a = coverage * tint.a * col.a;
  outColor = vec4(rgb * col.rgb * a, a);
}
