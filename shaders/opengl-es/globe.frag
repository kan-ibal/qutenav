#version 300 es

precision highp float;

uniform samplerCube globe;

in float diffuse;
in vec3 texCoord;

out vec4 color;

void main() {
  vec4 texColor = texture(globe, normalize(texCoord));

  // Default deep ocean surface color fallback if cubemap texture is unassigned, transparent, or empty
  vec3 baseRgb = texColor.rgb;
  if (texColor.a <= 0.0 || dot(baseRgb, baseRgb) < 0.0001) {
    baseRgb = vec3(0.08, 0.18, 0.32); // Deep maritime ocean blue
  }

  // Diffuse lighting scales surface RGB; alpha remains fully opaque (1.0)
  color = vec4(diffuse * baseRgb, 1.0);
}
