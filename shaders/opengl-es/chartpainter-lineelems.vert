#version 300 es
// Converted for broad hardware compatibility (UBO)
// Original source: https://github.com/paulhoux/Cinder-Samples/blob/master/GeometryShader/assets/shaders/lines1.geom
precision highp float;
precision highp int;

const float MITER_LIMIT = 0.75;
#define MAX_VEC4_VERTICES 1024
#define MAX_UVEC4_INDICES 1024

uniform float depth;
uniform uint vertexOffset;
uniform uint indexOffset;
uniform float lineWidth; // the line width in display (mm) units
uniform mat4 m_model;
uniform mat4 m_p;
uniform float windowScale; // transform between display (mm) and chart (m) units

out float texCoord;
flat out float segmentLength;

// Packed Vertex UBO: 2 x vec2 per vec4 (zero waste in std140)
layout(std140) uniform VertexBufferIn {
  vec4 data[MAX_VEC4_VERTICES];
} vertexBufferIn;

// Packed Index UBO: 4 x uint per uvec4 (std140 uint[] would waste 75% memory with 16B alignment!)
layout(std140) uniform IndexBufferIn {
  uvec4 data[MAX_UVEC4_INDICES];
} indexBufferIn;

// Helper to fetch vec2 vertex from packed vec4
vec2 getVertex(uint idx) {
  uint chunkIdx = idx >> 1u;
  vec4 chunk = vertexBufferIn.data[chunkIdx];
  return ((idx & 1u) == 0u) ? chunk.xy : chunk.zw;
}

// Helper to fetch uint index from packed uvec4
uint getIndex(uint idx) {
  uint chunkIdx = idx >> 2u; // idx / 4
  uint comp = idx & 3u;      // idx % 4
  uvec4 chunk = indexBufferIn.data[chunkIdx];
  if (comp == 0u) return chunk.x;
  if (comp == 1u) return chunk.y;
  if (comp == 2u) return chunk.z;
  return chunk.w;
}

void main() {
  float HW = lineWidth / windowScale;

  uint index = uint(gl_VertexID / 2);
  uint i0 = vertexOffset + getIndex(indexOffset + index);
  uint i1 = vertexOffset + getIndex(indexOffset + index + 1u);
  uint i2 = vertexOffset + getIndex(indexOffset + index + 2u);

  vec4 p0 = m_model * vec4(getVertex(i0), depth, 1.0);
  vec4 p1 = m_model * vec4(getVertex(i1), depth, 1.0);
  vec4 p2 = m_model * vec4(getVertex(i2), depth, 1.0);

  // determine the direction of the 2 segments (previous, next)
  vec2 v0 = normalize(p1.xy - p0.xy);
  vec2 v1 = normalize(p2.xy - p1.xy);

  // determine the normal of the 2 segments (previous, next)
  vec2 n0 = vec2(-v0.y, v0.x);
  vec2 n1 = vec2(-v1.y, v1.x);

  if (dot(v0, v1) < -MITER_LIMIT) {
    if (gl_VertexID % 2 == 0) {
      gl_Position = m_p * vec4(p1.xy + HW * (v0 + n0), p1.z, 1.0);
    } else {
      gl_Position = m_p * vec4(p1.xy + HW * (v0 - n0), p1.z, 1.0);
    }
  } else {
    // determine miter lines by averaging the normals of the 2 segments
    vec2 miter = normalize(n0 + n1);

    // determine the length of the miter from a right angled triangle (miter, n, v)
    float len_m = HW / dot(miter, n0);

    if (gl_VertexID % 2 == 0) {
      gl_Position = m_p * vec4(p1.xy + len_m * miter, p1.z, 1.0);
    } else {
      gl_Position = m_p * vec4(p1.xy - len_m * miter, p1.z, 1.0);
    }
  }

  texCoord = float((gl_VertexID / 2) % 2);
  segmentLength = length(p1.xy - p0.xy) * windowScale;
}