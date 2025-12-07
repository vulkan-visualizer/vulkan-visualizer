#version 460

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

// Wireframe box edges (12 edges = 24 vertices)
const vec3 kBoxEdges[24] = vec3[](
    // Bottom face edges
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5),  // Front edge
    vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),  // Right edge
    vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5),  // Back edge
    vec3(-0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5),  // Left edge

    // Top face edges
    vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),  // Front edge
    vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),  // Right edge
    vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5),  // Back edge
    vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5),  // Left edge

    // Vertical edges
    vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5),  // Front-left
    vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5),  // Front-right
    vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),  // Back-right
    vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5)   // Back-left
);

layout(location = 0) out vec3 vColor;

void main() {
    vec3 pos = kBoxEdges[gl_VertexIndex];
    gl_Position = pc.mvp * vec4(pos, 1.0);

    // White wireframe
    vColor = vec3(1.0, 1.0, 1.0);
}

