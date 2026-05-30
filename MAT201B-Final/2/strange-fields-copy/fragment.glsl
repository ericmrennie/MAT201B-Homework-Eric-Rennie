#version 400

uniform float b;
in float w;
out vec4 fragmentColor;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float map(float value, float low, float high, float Low, float High) {
  return Low + (High - Low) * ((value - low) / (high - low));
}

void main() {
  fragmentColor = vec4(hsv2rgb(vec3(map(w, -b, b, 0, 1), 1, 1)), 1);
}