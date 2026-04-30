precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  vec2 uv = v_uv - 0.5;
  float vign = 1.0 - dot(uv, uv) * 1.2;
  col *= vign;
  col = col * vec3(0.9, 0.95, 1.1) + vec3(0.0,0.02,0.08);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
