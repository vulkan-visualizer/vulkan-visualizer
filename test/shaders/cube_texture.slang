// ============================
// Vertex input / output
// ============================

struct VSInput {
    float3 position : LOCATION0;
    float4 color : LOCATION1;
    float2 uv : LOCATION2;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

// ============================
// Push constants
// ============================

struct Push {
    column_major float4x4 mvp;
};

[[vk::push_constant]]
cbuffer PushConstants {
    Push pc;
};

// ============================
// Descriptors (set = 0)
// ============================

[[vk::binding(0, 0)]]
Texture2D<float4> g_texture;

[[vk::binding(1, 0)]]
SamplerState g_sampler;

// ============================
// Vertex shader
// ============================

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    o.position = mul(pc.mvp, float4(input.position, 1.0));
    o.color    = input.color;
    o.uv       = input.uv;
    return o;
}

// ============================
// Fragment shader
// ============================

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target {
    float4 tex = g_texture.Sample(g_sampler, input.uv);
    return tex * input.color;
}


