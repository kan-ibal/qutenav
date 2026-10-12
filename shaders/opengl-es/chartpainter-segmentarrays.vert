#version 300 es
// Converted for broad hardware compatibility (UBO)
precision highp float;
precision highp int;

// 1024 vec4s = 2048 vec2 vertices (16 KB, guaranteed minimum GL_MAX_UNIFORM_BLOCK_SIZE)
#define MAX_VEC4_VERTICES 1024

uniform float depth;
uniform uint vertexOffset;
uniform float lineWidth; // the line width in display (mm) units
uniform mat4 m_model;
uniform mat4 m_p;
uniform float windowScale; // transform between display (mm) and chart (m) units

out float texCoord;
flat out float segmentLength;

// std140 rule: An array of vec2 incurs a 16-byte stride per element (50% padding waste).
// Packing 2 vec2 positions per vec4 provides 100% dense contiguous memory matching Float32Array!
layout(std140) uniform VertexBufferIn {
  vec4 data[MAX_VEC4_VERTICES];
} vertexBufferIn;

// Helper to unpack vec2 vertex coordinate from packed vec4 buffer
vec2 getVertex(uint idx) {
  uint chunkIdx = idx >> 1u; // idx / 2
  vec4 chunk = vertexBufferIn.data[chunkIdx];
  return ((idx & 1u) == 0u) ? chunk.xy : chunk.zw;
}

void main() {
  float HW = 0.5 * lineWidth / windowScale;
  uint si = 2u * uint(gl_VertexID / 6); // segment index

  vec4 p[2];
  p[0] = m_model * vec4(getVertex(vertexOffset + si), depth, 1.0);
  p[1] = m_model * vec4(getVertex(vertexOffset + si + 1u), depth, 1.0);

  // direction unit vector
  vec2 u = normalize(p[1].xy - p[0].xy);
  // ccw normal unit vector
  vec2 n = vec2(-u.y, u.x);

  int vi = gl_VertexID % 6;
  if (vi > 3) {
    vi = 2 * (vi - 4);
  }

  float a = (vi % 3 == 0) ? HW : -HW;
  int i = (vi > 1) ? 1 : 0;
  gl_Position = m_p * vec4(p[i].xy + a * n, p[0].z, 1.0);

  texCoord = float(i);
  segmentLength = length(p[1].xy - p[0].xy) * windowScale;
}