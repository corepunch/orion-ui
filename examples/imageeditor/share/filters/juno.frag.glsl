precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col *= vec3(1.04, 1.0, 0.9);
  float lum = dot(col, vec3(0.299,0.587,0.114));
  col += (1.0 - col) * (1.0 - lum) * 0.12;
  col = clamp(col, 0.0, 1.0);
  col = pow(col, vec3(0.95));
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
