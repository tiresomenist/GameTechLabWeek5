cbuffer TransformConstants : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 VP;

    float4 ImpostorCenterWS;
    float4 ImpostorSize;
    float4 ImpostorUV;
    float4 ImpostorCameraLocation;
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
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;

    float3 Center = ImpostorCenterWS.xyz;

    float3 ToCamera = ImpostorCameraLocation.xyz - Center;

    float LengthSq = dot(ToCamera, ToCamera);

    if (LengthSq < 0.000001f)
    {
        ToCamera = float3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        ToCamera /= sqrt(LengthSq);
    }
    
    float3 WorldUp = float3(0.0f, 0.0f, 1.0f);
    
    if (abs(ToCamera.z) > 0.99f)
    {
        WorldUp = float3(0.0f, 1.0f, 0.0f);
    }

    float3 Right = normalize(cross(ToCamera, WorldUp));

    float3 Up = normalize(cross(Right, ToCamera));

    float3 WorldPosition = Center + Right * Input.Position.x * ImpostorSize.x + Up * Input.Position.y * ImpostorSize.y;
    
    Output.Position = mul(float4(WorldPosition, 1.0f), VP);

    Output.Color = Input.Color * DiffuseColor;

    Output.UV = Input.UV * ImpostorUV.xy + ImpostorUV.zw;

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