#version 150 core

in vec2 position;
in vec2 texcoord;
in vec4 color;

out vec2 tex;
out vec4 col;

uniform mat4 projection;
uniform vec2 offset;
uniform vec2 scale;
uniform vec2 uv_offset;
uniform vec2 uv_scale;

void main() {
  col = color;
  tex = texcoord * uv_scale + uv_offset;
  gl_Position = projection * vec4(position * scale + offset, 0.0, 1.0);
}
