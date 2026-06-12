// GLSL written by Karl Yerkes

#version 400

layout(points) in;
layout(triangle_strip, max_vertices = 3) out;

uniform mat4 al_ModelViewMatrix;
uniform mat4 al_ProjectionMatrix;
uniform float r;
in vec3 normal[];
in float W[];
out float w;

void main() {
  mat4 m = al_ProjectionMatrix;   // rename to make lines shorter
  vec4 v = gl_in[0].gl_Position;  // al_ModelViewMatrix * gl_Position
  
  w = W[0]; // copy; why does it have to be an array in a geometry shader?

  vec3 up = normalize(cross(vec3(1, 0, 0), normal[0]));

  gl_Position = m * v;
  EmitVertex();

  gl_Position = m * (v + vec4(normal[0] * r, 0));
  EmitVertex();

  gl_Position = m * (v + vec4(up * r * 0.5, 0));
  EmitVertex();

  EndPrimitive();
}