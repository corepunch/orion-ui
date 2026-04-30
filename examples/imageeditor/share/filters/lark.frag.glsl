precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col = col * vec3(1.02, 1.03, 0.95);
  col = mix(col, col * col * (3.0 - 2.0*col), 0.2);
  col = clamp(col + vec3(0.04, 0.03, -0.04), 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
