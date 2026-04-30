#version 150 core

out vec4 outColor;

uniform sampler2D tex0;
uniform float u_mix;
in vec2 tex;

void main(){
  vec4 c = texture(tex0, tex);
  float lum = dot(c.rgb, vec3(0.299,0.587,0.114));
  lum = pow(lum, 1.1);
  lum = lum * 1.1 - 0.04;
  outColor = vec4(mix(c.rgb, vec3(clamp(lum,0.0,1.0)), u_mix), c.a);
}
