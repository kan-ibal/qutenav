#version 300 es
// Uniform Buffer Object (std140) version for outliner fragment shader
precision highp float;

layout (std140) uniform ColorBlock {
  vec4 base_color;
} colors;

out vec4 color;

void main() {
  vec4 c = colors.base_color;
  if (c.a <= 0.0) {
    c = (c.r > 0.0 || c.g > 0.0 || c.b > 0.0) ? vec4(c.rgb, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
  }
  color = c;
}
