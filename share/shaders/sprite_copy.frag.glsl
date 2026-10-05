float srgb_to_linear(float x) {
  return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

vec4 frag() {
  vec4 outColor;
  vec4 src = texture(tex0, tex) * col;
  vec3 tint_linear = vec3(srgb_to_linear(tint.r), srgb_to_linear(tint.g),
                          srgb_to_linear(tint.b));
  float alpha_scale = tint.a * alpha;
  vec3 rgb = src.rgb * tint_linear;
  if (params1.x > 0.5) rgb *= alpha_scale;
  outColor = vec4(rgb, src.a * alpha_scale);
  return outColor;
}
