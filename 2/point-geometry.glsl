#version 400

// take in a point and output a triangle strip with 4 vertices (aka a "quad")
// tells the GPU the geometry shader's imput and output types
// each invocation of this shader receives one point (matching Mesh::POINTS from the C++ code)
layout(points) in;
// it will output up to 4 vertices arranged as a triangle strip
// two triangles sharing an edge, which together form a quad/square
layout(triangle_strip, max_vertices = 4) out;
// XXX ~ what about winding? should use triangles?

// transforms 3D coordinates into 2D screen space
uniform mat4 al_ProjectionMatrix;
uniform float pointSize;

// input interface block - receives data from the vertex shader; previous stage in the pipeline
in Vertex {
  vec4 color;
  float size;
}
vertex[];

// data this shader sends forward to the fragment shader (the next stage)
// For each vertex it emits, it passes along a color and a mapping — a 2D coordinate in [-1, 1] 
// space representing where on the quad that vertex sits. The fragment shader can use mapping to do things like make the quad circular.
out Fragment {
  vec4 color;
  vec2 mapping;
}
fragment;

void main() {
  mat4 m = al_ProjectionMatrix;   // rename to make lines shorter
  vec4 v = gl_in[0].gl_Position;  // al_ModelViewMatrix * gl_Position (position of the input point)

  // calculates the radius of the quad
  // Starts with the uniform pointSize from C++, then multiplies by the per-vertex size 
  // (which came from texCoord() in the C++ code — remember pow(m, 1.0/3)). So bigger mass = bigger point.
  float r = pointSize;
  r *= vertex[0].size;

  // Each block follows the same pattern — offset the center position by ±r, set the color, set the mapping coordinate, then call EmitVertex()
  // bottom-left corner of the quad
  gl_Position = m * (v + vec4(-r, -r, 0.0, 0.0));
  fragment.color = vertex[0].color;
  fragment.mapping = vec2(-1.0, -1.0);
  EmitVertex();

  // bottom-right corner
  gl_Position = m * (v + vec4(r, -r, 0.0, 0.0));
  fragment.color = vertex[0].color;
  fragment.mapping = vec2(1.0, -1.0);
  EmitVertex();

  // top-left corner
  gl_Position = m * (v + vec4(-r, r, 0.0, 0.0));
  fragment.color = vertex[0].color;
  fragment.mapping = vec2(-1.0, 1.0);
  EmitVertex();

  // top-right corner
  gl_Position = m * (v + vec4(r, r, 0.0, 0.0));
  fragment.color = vertex[0].color;
  fragment.mapping = vec2(1.0, 1.0);
  EmitVertex();

  // Tells the GPU "that's all 4 vertices, finish the triangle strip." 
  // The GPU then draws 2 triangles: corners 1-2-3 and corners 2-3-4, which together make the full quad.
  EndPrimitive();
}
