#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in float vOccupied;

layout(location = 0) out vec4 outColor;

void main() {
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(vNormal);

    float ambient = 0.3;
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.7;
    float lighting = ambient + diffuse;

    vec3 finalColor = vColor * lighting;

    // For wireframe mode, enhance occupied voxels
    if (vOccupied > 0.5) {
        // Add slight glow to occupied cells
        finalColor += vec3(0.1, 0.15, 0.1);
    } else {
        // Darken empty cells
        finalColor *= 0.4;
    }

    // Highlight back faces
    if (!gl_FrontFacing) {
        finalColor = vec3(1.0, 0.1, 0.1);
    }

    outColor = vec4(finalColor, 1.0);
}

