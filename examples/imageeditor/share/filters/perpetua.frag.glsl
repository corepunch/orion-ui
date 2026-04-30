precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col.r = mix(col.r, pow(col.r, 0.85), 0.5);
  col.b = mix(col.b, col.b * 1.1 + 0.05, 0.5);
  col.g = mix(col.g, col.g * 1.04, 0.5);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
