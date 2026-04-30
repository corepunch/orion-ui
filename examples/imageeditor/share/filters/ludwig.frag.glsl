precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col = pow(col, vec3(0.92));
  col = col * vec3(1.02, 1.0, 0.96);
  col = clamp(col * 1.06 - 0.03, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
