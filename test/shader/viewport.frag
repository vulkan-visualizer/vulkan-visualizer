#version 460
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 0) out vec4 outColor;

void main() {
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(vNormal);

    float ambient = 0.3;
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.7;
    float lighting = ambient + diffuse;

    vec3 finalColor = vColor * lighting;
    outColor = vec4(finalColor, 1.0);
}