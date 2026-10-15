#version 300 es
precision highp float;

layout (location = 0) in vec3 vertex;

uniform mat4 m_pv;
uniform vec3 lp;

out float diffuse;
out vec3 texCoord;

void main() {
  // Safe surface normal (unit sphere)
  vec3 normal = (length(vertex) > 0.0001) ? normalize(vertex) : vec3(0.0, 0.0, 1.0);

  // Safe light direction: default to forward camera light (0,0,1) if lp is unset or zero
  vec3 lightDir = (length(lp) > 0.0001) ? normalize(lp) : vec3(0.0, 0.0, 1.0);

  // Compute lighting with ambient term (0.35) so the unlit hemisphere is never pitch black / invisible
  const float ambient = 0.35;
  float nDotL = max(0.0, dot(normal, lightDir));
  diffuse = ambient + (1.0 - ambient) * nDotL;

  // Transform strictly by combined projection-view matrix to eliminate the superfluous non-rotating sphere
  gl_Position = m_pv * vec4(vertex, 1.0);

  // Normalized cubemap direction vector (maps Z-up coordinates to Y-up cubemap)
  texCoord = normalize(vec3(vertex.x, vertex.z, -vertex.y));
}
