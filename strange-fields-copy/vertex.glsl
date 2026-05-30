#version 400

layout(location = 0) in vec4 aPosition;

uniform mat4 al_ModelViewMatrix;
uniform mat4 al_ProjectionMatrix;

out vec3 normal;
out float W;

// rotate a vector by Euler angles
//
// vec3 rotateEuler(vec3 v, float alpha, float beta, float gamma) {
//     float ca = cos(alpha);
//     float sa = sin(alpha);
//     float cb = cos(beta);
//     float sb = sin(beta);
//     float cg = cos(gamma);
//     float sg = sin(gamma);

//     mat3 Rx = mat3(
//         1.0, 0.0, 0.0,
//         0.0, ca,  sa,
//         0.0, -sa, ca
//     );

//     mat3 Ry = mat3(
//         cb, 0.0, -sb,
//         0.0, 1.0, 0.0,
//         sb, 0.0, cb
//     );

//     mat3 Rz = mat3(
//         cg,  sg, 0.0,
//        -sg,  cg, 0.0,
//         0.0, 0.0, 1.0
//     );

//     return Rz * Ry * Rx * v;
// }

// rotate a vector by Euler angles
//
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
  gl_Position = al_ModelViewMatrix * vec4(aPosition.xyz, 1.0);
//   normal = rotateEuler(
//     vec3(0, 0, 1),
//     atan(aPosition.w, aPosition.x),
//     atan(aPosition.w, aPosition.y),
//     atan(aPosition.w, aPosition.z)
//   );
  normal = rotateYXZ(
    vec3(1, 0, 0),
    //vec3(0, 0, 1),
    //vec3(0, 1, 0),
    vec3(
        atan(aPosition.w, aPosition.x),
        atan(aPosition.w, aPosition.y),
        atan(aPosition.w, aPosition.z)
    )
  );
  W = length(aPosition);
}