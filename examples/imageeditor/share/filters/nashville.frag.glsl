precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col = col * vec3(1.0, 0.9, 0.85) + vec3(0.05, 0.02, 0.0);
  col = mix(col, col * col * (3.0 - 2.0*col), 0.15);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
