vec4 frag() {
  vec4 outColor;
  vec4 t = ui_texture(tex);
  // Glyph coverage (params0.y): light text gains weight on a dark ground as a
  // browser draws it (Skia's luminance-dependent text contrast, fitted to
  // Chrome: the same ink for white and black 13px IBM Plex Sans), dark text a little.
  if (params0.y > 0.5) {
    float g = mix(1.30, 2.66, dot(col.rgb, vec3(0.2126, 0.7152, 0.0722)));
    t.a = 1.0 - pow(1.0 - clamp(t.a, 0.0, 1.0), g);
  }
  vec4 src = t * col;
  vec3 tint_rgb = tint.rgb;
  float alpha_scale = tint.a * alpha;
  vec3 rgb = src.rgb * tint_rgb;
  if (params1.x > 0.5) rgb *= alpha_scale;
  outColor = vec4(rgb, src.a * alpha_scale);
  return ui_clipped(outColor);
}
