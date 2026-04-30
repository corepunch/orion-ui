precision mediump float;
uniform sampler2D u_tex;
uniform float u_mix;
varying vec2 v_uv;

void main(){
  vec4 c = texture2D(u_tex, v_uv);
  float lum = dot(c.rgb, vec3(0.299,0.587,0.114));
  lum = pow(lum, 1.1);
  lum = lum * 1.1 - 0.04;
  gl_FragColor = vec4(mix(c.rgb, vec3(clamp(lum,0.0,1.0)), u_mix), c.a);
}
