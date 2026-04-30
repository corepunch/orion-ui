#version 150 core

in vec2 position;
in vec2 texcoord;
in vec4 color;

out vec2 tex;
out vec4 col;

void main() {
  col = color;
  tex = texcoord;
  gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
