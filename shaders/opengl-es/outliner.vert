#version 300 es
// Uniform Buffer Object (std140) version for outliner vertex shader
precision highp float;
precision highp int;

layout (location = 0) in vec3 vertex;

layout (std140) uniform MatrixBlock {
  mat4 m_pv;
  mat4 m_p;
  mat4 m_model;
  float depth;
} matrices;

void main() {
  vec4 pos = vec4(vertex, 1.0);

  if (matrices.m_model[3][3] != 0.0 && matrices.m_model[0][0] != 0.0) {
    pos = matrices.m_model * pos;
  }

  if (matrices.m_pv[0][0] != 0.0 || matrices.m_pv[1][1] != 0.0 || matrices.m_pv[3][3] != 0.0 || matrices.m_pv[2][3] != 0.0) {
    gl_Position = matrices.m_pv * pos;
  } else if (matrices.m_p[0][0] != 0.0 || matrices.m_p[1][1] != 0.0) {
    gl_Position = matrices.m_p * pos;
  } else {
    gl_Position = pos;
  }

  // Depth bias in clip space towards camera (-0.002) to prevent Z-fighting
  gl_Position.z -= 0.002 * gl_Position.w;
}
