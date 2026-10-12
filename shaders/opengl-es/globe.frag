#version 300 es
// Uniform Buffer Object (std140) version for globe fragment shader
precision highp float;

uniform samplerCube globe;

in float diffuse;
in vec3 texCoord;

out vec4 color;

void main() {
  vec4 texColor = texture(globe, normalize(texCoord));
  float alpha = (texColor.a > 0.0) ? texColor.a : 1.0;
  color = vec4(diffuse * texColor.rgb, alpha);
}
