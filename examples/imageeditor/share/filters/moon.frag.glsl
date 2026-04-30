precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  float lum = dot(c.rgb, vec3(0.299,0.587,0.114));
  vec3 bw = vec3(lum);
  bw = pow(bw, vec3(0.9));
  bw = bw * 1.1 - 0.05;
  bw = clamp(bw, 0.0, 1.0);
  gl_FragColor = vec4(mix(c.rgb, bw, u_mix), c.a);
}
