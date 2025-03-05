
struct matrixInstance
{
    float4x4 m_mtx;
};

ConstantBuffer<matrixInstance> camera : register(b0);
ConstantBuffer<matrixInstance> worldMat : register(b1);

Texture2D g_texture : register(t0);
SamplerState staticSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(mul(float4(input.position, 1.0f), worldMat.m_mtx), camera.m_mtx);
    //output.position = float4(input.position, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_TARGET
{
    float4 color = g_texture.Sample(staticSampler, input.uv);
    //float4 color = float4(0.5, 1.0, 0.5, 1.0);
    if(color.a == 0.0)
        discard;
    return color;
}