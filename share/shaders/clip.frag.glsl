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

