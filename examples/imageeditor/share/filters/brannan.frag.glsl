precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col = mix(col, vec3(dot(col,vec3(0.299,0.587,0.114))), 0.2);
  col = col * vec3(0.95, 0.9, 1.0) + vec3(0.04, 0.0, 0.06);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
