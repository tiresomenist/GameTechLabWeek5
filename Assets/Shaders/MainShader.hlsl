// MainShader.hlsl
cbuffer constants : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 VP;
}


struct VS_INPUT
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
    //MVP행렬 곱으로 위치 변환
    float4 WorldPos = mul(float4(input.position.xyz, 1.0f), World);
    output.position = mul(WorldPos, VP);
    
    output.color = input.color;
    
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    return input.color;
}

VS_OUTPUT VS_Highlight(VS_INPUT input)
{
    VS_OUTPUT output;
    
    float3 expandedPos = input.position.xyz * 1.05f;
    
    float4 WorldPos = mul(float4(expandedPos, 1.0f), World);
    output.Pos = mul(WorldPos, VP);
    
    return output;
}

float4 PS_Highlight(VS_OUTPUT input) : SV_Target
{
    return float4(0.738f, 0.270f, 0.012f, 1.0f);
}
