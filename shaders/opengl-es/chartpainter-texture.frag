#version 300 es

precision highp float;

// Note: 'layout (binding = 0)' is not permitted in GLSL ES 3.00 (causes error S0059).
// Binding points for samplers in ES 3.00 are bound via host CPU API:
// glUniform1i(glGetUniformLocation(program, "atlas"), 0);
uniform sampler2D atlas;

in vec2 tex;
layout (location = 0) out vec4 color;

void main() {
  color = texture(atlas, tex);
}
