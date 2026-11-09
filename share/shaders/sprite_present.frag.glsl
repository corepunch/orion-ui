// The composed screen is sRGB-encoded already; a framebuffer that encodes on
// write (an sRGB surface on OpenGL ES) gets it decoded first (params0.x).
vec4 frag() {
  vec4 c = texture(tex0, tex);
  vec3 rgb = c.rgb;
  if (params0.x > 0.5) rgb = vec3(ui_decode(c.r), ui_decode(c.g), ui_decode(c.b));
  return vec4(rgb, 1.0);
}
