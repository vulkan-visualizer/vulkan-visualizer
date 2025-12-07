#version 460
// 3D viewport shader with MVP transform
layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pc;

// Cube vertices (8 corners)
const vec3 kCubePositions[36] = vec3[](
    // Front face
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5), vec3(-0.5, -0.5,  0.5),
    // Back face
    vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3(-0.5, -0.5, -0.5),
    // Top face
    vec3(-0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    // Bottom face
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5),
    // Right face
    vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5, -0.5, -0.5),
    // Left face
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5), vec3(-0.5, -0.5, -0.5)
);

const vec3 kCubeNormals[36] = vec3[](
    // Front
    vec3( 0,  0,  1), vec3( 0,  0,  1), vec3( 0,  0,  1),
    vec3( 0,  0,  1), vec3( 0,  0,  1), vec3( 0,  0,  1),
    // Back
    vec3( 0,  0, -1), vec3( 0,  0, -1), vec3( 0,  0, -1),
    vec3( 0,  0, -1), vec3( 0,  0, -1), vec3( 0,  0, -1),
    // Top
    vec3( 0,  1,  0), vec3( 0,  1,  0), vec3( 0,  1,  0),
    vec3( 0,  1,  0), vec3( 0,  1,  0), vec3( 0,  1,  0),
    // Bottom
    vec3( 0, -1,  0), vec3( 0, -1,  0), vec3( 0, -1,  0),
    vec3( 0, -1,  0), vec3( 0, -1,  0), vec3( 0, -1,  0),
    // Right
    vec3( 1,  0,  0), vec3( 1,  0,  0), vec3( 1,  0,  0),
    vec3( 1,  0,  0), vec3( 1,  0,  0), vec3( 1,  0,  0),
    // Left
    vec3(-1,  0,  0), vec3(-1,  0,  0), vec3(-1,  0,  0),
    vec3(-1,  0,  0), vec3(-1,  0,  0), vec3(-1,  0,  0)
);

const vec3 kCubeColors[6] = vec3[](
    vec3(1.0, 0.2, 0.2),  // Front - Red
    vec3(0.2, 1.0, 0.2),  // Back - Green
    vec3(0.2, 0.2, 1.0),  // Top - Blue
    vec3(1.0, 1.0, 0.2),  // Bottom - Yellow
    vec3(1.0, 0.2, 1.0),  // Right - Magenta
    vec3(0.2, 1.0, 1.0)   // Left - Cyan
);

void main() {
    vec3 pos = kCubePositions[gl_VertexIndex];
    vec3 normal = kCubeNormals[gl_VertexIndex];
    int faceIndex = gl_VertexIndex / 6;

    gl_Position = pc.mvp * vec4(pos, 1.0);
    vWorldPos = (pc.model * vec4(pos, 1.0)).xyz;
    vNormal = mat3(pc.model) * normal;
    vColor = kCubeColors[faceIndex];
}