cbuffer TransformConstants : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 VP;
};
cbuffer TextureDrawConstants : register(b1)
{
    float2 UVScale;
    float2 UVOffset;

    float4 DiffuseColor;
    
    float AlphaCutoff;
    float3 Padding;
};
Texture2D ObjectTexture : register(t0);
SamplerState TextureSampler : register(s0);

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;

    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    // 추후 빛과 그림자 계산하려면 World 와 ViewProjection이 따로 전달 되야 함
    Output.Position = mul(WorldPosition, VP);
    Output.Normal = Input.Normal;
    Output.Color = Input.Color * DiffuseColor;
    Output.UV = Input.UV * UVScale + UVOffset;

    return Output;
}

float4 mainPS(PS_INPUT Input) : SV_TARGET
{
    float4 Color = ObjectTexture.Sample(TextureSampler, Input.UV) * Input.Color;

    if (AlphaCutoff > 0.0f)
    {
        clip(Color.a - AlphaCutoff);
    }

    return Color;
}
