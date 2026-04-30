precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  float lum = dot(col, vec3(0.299,0.587,0.114));
  col = mix(col, vec3(lum), 0.2);
  col = col * vec3(1.0, 0.95, 0.88) + vec3(0.04, 0.02, 0.0);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
