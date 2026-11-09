// Orion composites as CSS does: colours stay sRGB-encoded, premultiplied in
// that encoding, and blend as they are. A texture GL stores as sRGB (a 3D
// view's linear rendering) is sampled decoded; ui_texture() encodes it again
// when `decode` says so.
float ui_encode(float x) {
  x = max(x, 0.0);
  return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
float ui_decode(float x) {
  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}
vec4 ui_texture(vec2 uv) {
  vec4 s = texture(tex0, uv);
  if (decode < 0.5) return s;
  vec3 c = s.a > 0.0 ? s.rgb / s.a : vec3(0.0);
  return vec4(vec3(ui_encode(c.r), ui_encode(c.g), ui_encode(c.b)) * s.a, s.a);
}

// Rounded clipping shared by the UI sprite programs (set_clip_rounded_rect).
// clip_rect is x, y, width, height in framebuffer pixels with the GL origin at
// the bottom left, as the scissor box is; clip_radius is its corner radius in
// pixels, and 0 turns the clip off. Coverage is antialiased over one pixel.
vec4 ui_clipped(vec4 color) {
  if (clip_radius <= 0.0) return color;
  vec2 half_size = clip_rect.zw * 0.5;
  vec2 p = gl_FragCoord.xy - (clip_rect.xy + half_size);
  float r = min(clip_radius, min(half_size.x, half_size.y));
  vec2 q = abs(p) - half_size + vec2(r);
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
  return color * (1.0 - smoothstep(-0.5, 0.5, d));
}

