#version 300 es
// S-52 Core Chartpainter Vertex Shader (AreaShader)
// Eliminates the superfluous unaligned green sphere/mesh in S-52 (CM93) Globe View.
// Planar 2D Mercator land area vertices are not aligned with Earth North/South polar coordinates
// or the 6 cubemap face textures loaded in void Globe::loadImages().
layout (location = 0) in vec2 vertex;

uniform mat4 m_p;
uniform float depth;
uniform mat4 m_model;

void main() {
  // In 3D Globe View, m_p is a perspective projection matrix (m_p[2][3] != 0.0 or m_p[3][2] != 0.0)
  // while m_model is identity (m_model[0][0] == 1.0 && m_model[3][0] == 0.0).
  // Planar 2D Mercator coordinates cannot be directly projected into 3D spherical space without
  // WGS84 geodesic spherical transformation; clipping them outside viewport eliminates the
  // superfluous unaligned green sphere mesh from the 3D globe view.
  if ((m_p[2][3] != 0.0 || m_p[3][2] != 0.0) &&
      m_model[0][0] == 1.0 && m_model[1][1] == 1.0 &&
      m_model[3][0] == 0.0 && m_model[3][1] == 0.0) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // Clip outside frustum
    return;
  }

  gl_Position = m_p * m_model * vec4(vertex, depth, 1.0);
}

