vec4 frag() {
  vec4 outColor;
  float index = floor(texture(tex0, tex).a * 255.0 + 0.5);
  outColor = texture(palette_tex, vec2((index + 0.5) / 256.0, 0.5)) * col;
  outColor.rgb *= alpha;
  outColor.a *= alpha;
  return outColor;
}
