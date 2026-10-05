#version 150 core

in vec2 tex;
in vec4 col;
out vec4 outColor;
uniform sampler2D tex0;
uniform vec4 tint;
uniform vec4 params0; // logical size, radius, bevel
uniform vec4 params1; // reserved shadow margin, pressed, hover, selected
uniform vec4 glyph_uv; // atlas endpoints
uniform vec4 glyph_box; // logical origin and size
uniform vec4 shadow_color;
uniform float disabled;

vec3 srgb_to_linear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

float box_distance(vec2 p, vec2 half_size, float radius) {
  vec2 q = abs(p) - half_size + radius;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float glyph_mask(vec2 p) {
  vec2 uv = (p - glyph_box.xy) / max(glyph_box.zw, vec2(1.0));
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 0.0;
  return texture(tex0, mix(glyph_uv.xy, glyph_uv.zw, uv)).a;
}

void main() {
  vec2 size = params0.xy;
  float margin = params1.x;
  float pressed = params1.y * (1.0 - disabled);
  float hover = params1.z * (1.0 - disabled);
  float selected = params1.w * (1.0 - disabled);
  vec2 half_size = max(size * 0.5 - margin, vec2(0.5));
  float radius = min(params0.z, min(half_size.x, half_size.y));
  float bevel = min(params0.w, min(half_size.x, half_size.y));
  vec2 p = tex * size - size * 0.5 - vec2(0.0, pressed * min(0.6, margin * 0.3));
  float d = box_distance(p, half_size, radius);
  float aa = max(fwidth(d), 0.01);
  float face = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
  float shadow_d = box_distance(p - vec2(0.0, margin * 0.45 * (1.0 - pressed)), half_size, radius);
  float sigma = max(0.25, margin * 0.45);
  float shadow = exp(-0.5 * pow(max(0.0, shadow_d) / sigma, 2.0));
  vec2 gutter = min(tex * size, (1.0 - tex) * size);
  shadow *= smoothstep(0.0, max(aa, margin * 0.5), min(gutter.x, gutter.y));
  shadow *= step(0.01, margin) * mix(0.45, 0.22, pressed) * (1.0 - 0.5 * disabled);

  vec3 base = srgb_to_linear(tint.rgb);
  float gray = dot(base, vec3(0.2126, 0.7152, 0.0722));
  base = clamp(mix(vec3(gray), base, 1.25), 0.0, 1.0);
  base = mix(base, vec3(gray) * 0.7 + 0.10, disabled);
  bool has_glyph = glyph_box.z > 0.0 && glyph_box.w > 0.0;
  float gloss_k = (has_glyph ? 1.0 : 0.55) * (1.0 - 0.6 * disabled) * (1.0 - 0.55 * pressed);
  float y = clamp((p.y + half_size.y) / (2.0 * half_size.y), 0.0, 1.0);
  vec3 top = mix(pow(base, vec3(has_glyph ? 0.62 : 0.78)), vec3(1.0), (has_glyph ? 0.08 : 0.02) + hover * 0.10);
  vec3 bottom = pow(base, vec3(1.18)) * (0.62 + hover * 0.08);
  vec3 rgb = mix(top, bottom, smoothstep(0.0, 1.0, y));
  rgb = mix(rgb, pow(base, vec3(0.5)), smoothstep(0.72, 1.0, y) * 0.45 * gloss_k);
  rgb *= 1.0 - pressed * 0.22;
  float inner = -d;
  float band = 1.0 - smoothstep(has_glyph ? 0.40 : 0.22, 0.50, y);
  float gloss = band * mix(0.38, 0.10, y / 0.5) * smoothstep(0.5, 2.5, inner) * gloss_k;
  rgb = mix(rgb, vec3(1.0), gloss);
  vec2 q = abs(p) - half_size + radius;
  vec2 n = max(q, 0.0) * sign(p);
  if (dot(n, n) < 0.0001) n = q.x > q.y ? vec2(sign(p.x), 0.0) : vec2(0.0, sign(p.y));
  float light = dot(normalize(n), normalize(vec2(-0.4, -1.0)));
  float rim = 1.0 - smoothstep(0.0, max(aa, bevel), inner);
  light *= 1.0 - 1.65 * pressed;
  rgb = mix(rgb, light > 0.0 ? mix(pow(base, vec3(0.4)), vec3(1.0), 0.5) : pow(base, vec3(1.4)) * 0.35,
            rim * abs(light) * 0.55);
  float outline = 1.0 - smoothstep(0.0, aa + 0.45, inner);
  rgb = mix(rgb, pow(base, vec3(1.5)) * 0.35, outline * 0.6);
  float ring = 1.0 - smoothstep(0.8, 0.8 + aa, inner);
  rgb = mix(rgb, vec3(0.95), ring * selected * 0.9);

  if (has_glyph) {
    vec2 pixel = max(fwidth(tex * size), vec2(0.001));
    vec2 origin = floor(glyph_box.xy / pixel + 0.5) * pixel;
    float press_offset = floor(min(0.6, margin * 0.3) / pixel.y + 0.5) * pixel.y;
    vec2 gp = tex * size + glyph_box.xy - origin - vec2(0.0, pressed * press_offset);
    vec2 wall = max(floor(vec2(0.6) / pixel + 0.5), vec2(1.0)) * pixel;
    float mask = glyph_mask(gp);
    float upper = glyph_mask(gp - wall), lower = glyph_mask(gp + wall);
    float shade = mask * (1.0 - upper);   // upper/left inner wall faces away from the light
    float catchlight = mask * (1.0 - lower);
    float lip = lower * (1.0 - mask);     // face rim just above/left of the cavity
    vec3 floor_col = mix(mix(pow(base, vec3(0.35)), vec3(1.0), 0.62), vec3(0.62), disabled);
    float glint = glyph_mask(gp - wall) * (1.0 - mask); // face rim below/right of the cavity
    rgb = mix(rgb, pow(base, vec3(1.4)) * 0.3, lip * 0.6);
    rgb = mix(rgb, vec3(1.0), glint * 0.25);
    rgb = mix(rgb, floor_col, mask);
    rgb = mix(rgb, pow(base, vec3(1.3)) * 0.38, shade * 0.9);
    rgb = mix(rgb, vec3(1.0), catchlight * 0.85);
  }
  float a = face * tint.a * col.a;
  float sa = shadow * shadow_color.a * tint.a * col.a * (1.0 - face);
  outColor = vec4(rgb * col.rgb * a + shadow_color.rgb * sa, a + sa);
}
