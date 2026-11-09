vec4 frag() {
  vec4 outColor;
  vec4 src = ui_texture(tex) * col;
  vec4 left = params0;
  vec4 right = params1;
  left.rgb = left.rgb;
  right.rgb = right.rgb;
  vec4 grad = mix(left, right, clamp(tex.x, 0.0, 1.0));
  outColor = vec4(grad.rgb, grad.a * src.a * tint.a);
  outColor.a *= alpha;
  return ui_clipped(outColor);
}
