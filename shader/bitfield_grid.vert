#version 450
}
    vOccupied = iOccupied;
    vWorldPos = worldPos;
    vNormal = worldNormal;

    }
        vColor = mix(vColor, normPos * 0.5 + vec3(0.5), 0.3);
        vec3 normPos = normalize(iPosition + vec3(2.0));
    if (iOccupied > 0.5) {
    // Enhance color based on position for occupied voxels

    }
        vColor = vec3(0.15, 0.15, 0.2); // Dim gray for empty
    } else {
        vColor = vec3(0.0, 1.0, 0.8); // Bright cyan for occupied
    if (iOccupied > 0.5) {
    // Occupied = bright cyan/green, Empty = dim gray
    // Color based on occupancy for wireframe mode

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);

    vec3 worldNormal = normalize(aNormal);
    vec3 worldPos = aPosition + iPosition;
    // Transform vertex to world space
void main() {

} pc;
    mat4 viewProj;
layout(push_constant) uniform PushConstants {

layout(location = 3) out float vOccupied;
layout(location = 2) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 0) out vec3 vColor;

layout(location = 3) in float iOccupied; // 1.0 if occupied, 0.0 if empty (for wireframe mode)
layout(location = 2) in vec3 iPosition;  // Instance position (voxel center)
layout(location = 1) in vec3 aNormal;    // Vertex normal
layout(location = 0) in vec3 aPosition;  // Vertex position (cube mesh or grid)


