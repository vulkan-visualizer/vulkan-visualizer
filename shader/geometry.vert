#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 iPosition;
layout(location = 3) in vec3 iRotation;
layout(location = 4) in vec3 iScale;
layout(location = 5) in vec3 iColor;
layout(location = 6) in float iAlpha;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

mat3 rotateX(float angle) {
    float rad = radians(angle);
    float c = cos(rad);
    float s = sin(rad);
    return mat3(1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c);
}

mat3 rotateY(float angle) {
    float rad = radians(angle);
    float c = cos(rad);
    float s = sin(rad);
    return mat3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c);
}

mat3 rotateZ(float angle) {
    float rad = radians(angle);
    float c = cos(rad);
    float s = sin(rad);
    return mat3(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0);
}

void main() {
    mat3 rotation = rotateZ(iRotation.z) * rotateY(iRotation.y) * rotateX(iRotation.x);
    vec3 rotated = rotation * (aPosition * iScale);
    vec3 worldPos = rotated + iPosition;
    vec3 worldNormal = normalize(rotation * aNormal);
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    vColor = iColor;
    vNormal = worldNormal;
    vWorldPos = worldPos;
}

