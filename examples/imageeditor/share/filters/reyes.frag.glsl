precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  vec3 col = c.rgb;
  col = mix(col, vec3(dot(col, vec3(0.299,0.587,0.114))), 0.1);
  col = col * 0.9 + 0.1;
  col = mix(col, col * vec3(1.05, 1.02, 0.95), 0.5);
  col = clamp(col, 0.0, 1.0);
  col = mix(c.rgb, col, u_mix);
  gl_FragColor = vec4(col, c.a);
}
