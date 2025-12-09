#version 450

layout(location = 0) in vec3 aPosition;  // Vertex position (cube mesh)
layout(location = 1) in vec3 aNormal;    // Vertex normal
layout(location = 2) in vec3 iPosition;  // Instance position (voxel center)

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main() {
    // Transform vertex to world space
    vec3 worldPos = aPosition + iPosition;
    vec3 worldNormal = normalize(aNormal);

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);

    // Color based on position for visual distinction
    vColor = normalize(iPosition + vec3(2.0)) * 0.5 + vec3(0.5);
    vNormal = worldNormal;
    vWorldPos = worldPos;
}


