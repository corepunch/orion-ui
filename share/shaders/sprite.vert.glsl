vec4 vert() {
  col = color;
  tex = texcoord * uv_scale + uv_offset;
  return projection * vec4(position * scale + offset, 0.0, 1.0);
}
