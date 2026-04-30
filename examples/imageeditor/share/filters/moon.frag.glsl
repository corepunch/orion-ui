#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  float lum = dot(c.rgb, vec3(0.299,0.587,0.114));
  vec3 bw = vec3(lum);
  bw = pow(bw, vec3(0.9));
  bw = bw * 1.1 - 0.05;
  bw = clamp(bw, 0.0, 1.0);
  outColor = vec4(mix(c.rgb, bw, u_mix), c.a);
}
