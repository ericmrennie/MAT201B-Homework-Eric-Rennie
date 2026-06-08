#version 400
layout(location = 0) in vec4 aPosition;
uniform mat4 al_ModelViewMatrix;
uniform mat4 al_ProjectionMatrix;
uniform float b;            // wrap range — divide to normalize into [-1,1] before MVP
uniform float innerSpin;    // accumulated angle (degrees) for differential rotation
out vec3 normal;
out float W;

vec3 rotateYXZ(vec3 v, vec3 angles) {
  vec3 c = cos(angles);
  vec3 s = sin(angles);
  // 1. Rotate around Y axis (Yaw)
  v = vec3(
    v.x * c.y + v.z * s.y,
    v.y,
    -v.x * s.y + v.z * c.y
  );
  // 2. Rotate around X axis (Pitch)
  v = vec3(
    v.x,
    v.y * c.x - v.z * s.x,
    v.y * s.x + v.z * c.x
  );
  // 3. Rotate around Z axis (Roll)
  v = vec3(
    v.x * c.z - v.y * s.z,
    v.x * s.z + v.y * c.z,
    v.z
  );
  return v;
}

void main() {
  // Normalize from [-b,b] into [-1,1] before applying the view transform.
  // This is what the old `vertex / b` in onDraw was doing, but doing it here
  // means the CPU never sees exploding coordinates when b is small.
  vec3 pos = aPosition.xyz / b;

  // Differential rotation: each point rotates in the XY plane by an angle
  // proportional to its distance from the Z axis.  Points in the outer arms
  // of each spiral sweep faster than those near the core, making the spirals
  // appear to wind/unwind around their own centres.
  float dist = length(pos.xy);
  float spinRad = innerSpin * (3.14159265 / 180.0) * dist;
  float cosS = cos(spinRad);
  float sinS = sin(spinRad);
  pos.xy = vec2(pos.x * cosS - pos.y * sinS,
                pos.x * sinS + pos.y * cosS);

  gl_Position = al_ModelViewMatrix * vec4(pos, 1.0);

  normal = rotateYXZ(
    vec3(1, 0, 0),
    vec3(
      atan(aPosition.w, aPosition.x),
      atan(aPosition.w, aPosition.y),
      atan(aPosition.w, aPosition.z)
    )
  );
  W = length(aPosition);
}
