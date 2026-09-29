cbuffer TransformConstants : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 VP;
};

cbuffer MaterialConstants : register(b1)
{
    float4 DiffuseColor;

    float AlphaCutoff;
    float UseTexture;
    float2 Padding;
};

Texture2D ObjectTexture : register(t0);
SamplerState TextureSampler : register(s0);

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR;
    float2 UV       : TEXCOORD0;
};

PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    
    Output.Position = mul(float4(Input.Position, 1.0f), VP);

    Output.Color = float4(1, 1, 1, 1);
    Output.UV = Input.UV;

    return Output;
}

float4 mainPS(PS_INPUT Input) : SV_TARGET
{
    float4 Color = DiffuseColor;

    if (UseTexture > 0.5f)
    {
        Color *= ObjectTexture.Sample(
            TextureSampler,
            Input.UV
        );
    }

    Color *= Input.Color;
    
    if (AlphaCutoff > 0.0f)
    {
        clip(Color.a - AlphaCutoff);
    }
    
    Color.a = 1.0f;

    return Color;
}