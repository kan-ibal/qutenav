#version 300 es
precision highp float;
precision highp int;

layout (location = 0) in vec3 vertex;

// Uniform Buffer Object (std140) layout for broad hardware compatibility
layout (std140) uniform GlobeParamsBlock {
  mat4 m_pv;
  mat4 m_p;
  mat4 m_model;
  vec3 lp;
  float ambient;
};

out float diffuse;
out vec3 texCoord;

void main() {
  // Safe surface normal (unit sphere)
  vec3 normal = (length(vertex) > 0.0001) ? normalize(vertex) : vec3(0.0, 0.0, 1.0);

  // Safe light direction: default to forward camera light (0,0,1) if lp is unset or zero
  vec3 lightDir = (length(lp) > 0.0001) ? normalize(lp) : vec3(0.0, 0.0, 1.0);

  // Compute lighting with ambient term (0.35) so the unlit hemisphere is never pitch black / invisible
  float amb = (ambient > 0.0) ? ambient : 0.35;
  float nDotL = max(0.0, dot(normal, lightDir));
  diffuse = amb + (1.0 - amb) * nDotL;

  // Apply matrix transformations (m_pv combined or m_p + m_model)
  mat4 model = (m_model[3][3] != 0.0) ? m_model : mat4(1.0);
  if (m_pv[3][3] != 0.0 || m_pv[2][3] != 0.0) {
    gl_Position = m_pv * (model * vec4(vertex, 1.0));
  } else if (m_p[3][3] != 0.0 || m_p[2][3] != 0.0) {
    gl_Position = m_p * (model * vec4(vertex, 1.0));
  } else {
    gl_Position = model * vec4(vertex, 1.0);
  }

  // Normalized cubemap direction vector (maps Z-up coordinates to Y-up cubemap)
  texCoord = normalize(vec3(vertex.x, vertex.z, -vertex.y));
}

