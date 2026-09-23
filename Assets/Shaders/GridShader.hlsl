// GridShader.hlsl

cbuffer TransformBuffer : register(b0)
{
    row_major float4x4 MVP;
};

cbuffer GridConstants : register(b1)
{
    float3 CameraPos;
    int GridPlaneType ;      // 0:XY | 1:Z
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float4 Color : COLOR;
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : POSITION1;
    float3 LocalPos : TEXCOORD0;
};

VS_OUTPUT VS_Grid(VS_INPUT input)
{
    VS_OUTPUT output;
    float3 worldPos = input.Pos;

    if (GridPlaneType == 0) // XY 평면 (기존)
    {
        float2 gridOrigin = floor(CameraPos.xy);
        worldPos += float3(gridOrigin, 0.0f);
    }
    else if (GridPlaneType == 1) // YZ 평면 (Z축 전용)
    {
        float heightOffset = floor(CameraPos.x);
        worldPos += float3(heightOffset, 0.0f, 0.0f);
    }
    
    output.Pos = mul(float4(worldPos, 1.0f), MVP);
    output.WorldPos = worldPos;
    output.LocalPos = input.Pos;
    return output;
}

float4 PS_Grid(VS_OUTPUT input) : SV_Target
{
    // =====================================
    // Z축 전용 패스 — 그리드 없이 축 한 줄만, 양의 방향으로만
    // =====================================
    if (GridPlaneType == 1)
    {
        float axisThickness = 0.07f;
        float antiAliasWidth = max(fwidth(input.WorldPos.y), 0.0001f);

        float distToZAxis = abs(input.WorldPos.y);
        float zAxisWeight = 1.0f - smoothstep(axisThickness - antiAliasWidth, axisThickness + antiAliasWidth, distToZAxis);

        // 라인이 뻗어나가는 방향(local X, 회전 후 world Z)이 양수일 때만 표시
        float aaAlongLine = max(fwidth(input.WorldPos.x), 0.0001f);
        float positiveMaskZ = smoothstep(-aaAlongLine, aaAlongLine, -input.WorldPos.x);
        zAxisWeight *= positiveMaskZ;

        if (zAxisWeight <= 0.0f)
            discard;

        // Fade Out
        float dist = distance(CameraPos.zx, input.WorldPos.zx);
        float alpha = 1.0f - smoothstep(20.0f, 40.0f, dist);

        return float4(0.0f, 0.0f, 1.0f, alpha * zAxisWeight);
    }

    // =====================================
    // XY 평면 — 그리드 + X/Y축, 양의 방향으로만
    // =====================================
    float2 gridUV = input.LocalPos.xy;

    float thickness = 0.001f;
    float2 antiAliasWidth = max(fwidth(gridUV), float2(0.0001f, 0.0001f));

    float2 grid = abs(frac(gridUV - 0.5f) - 0.5f);
    float2 lineMask = 1.0f - smoothstep(thickness, thickness + antiAliasWidth, grid);
    float gridLine = max(lineMask.x, lineMask.y);

    float axisThickness = 0.07f;
    float distToYAxis = abs(input.WorldPos.x);
    float distToXAxis = abs(input.WorldPos.y);

    float yAxisWeight = 1.0f - smoothstep(axisThickness - antiAliasWidth.x, axisThickness + antiAliasWidth.x, distToYAxis);
    float xAxisWeight = 1.0f - smoothstep(axisThickness - antiAliasWidth.y, axisThickness + antiAliasWidth.y, distToXAxis);

    // Y축 라인은 y가 뻗어나가는 방향 → WorldPos.y >= 0 에서만 표시
    float positiveMaskY = smoothstep(-antiAliasWidth.y, antiAliasWidth.y, input.WorldPos.y);
    yAxisWeight *= positiveMaskY;

    // X축 라인은 x가 뻗어나가는 방향 → WorldPos.x >= 0 에서만 표시
    float positiveMaskX = smoothstep(-antiAliasWidth.x, antiAliasWidth.x, input.WorldPos.x);
    xAxisWeight *= positiveMaskX;

    float isAxis = max(xAxisWeight, yAxisWeight);

    if (gridLine <= 0.001f && isAxis <= 0.0f)
        discard;

    float dist = distance(CameraPos.xy, input.WorldPos.xy);
    float alpha = 1.0f - smoothstep(20.0f, 40.0f, dist);

    float4 finalColor = float4(0.3f, 0.3f, 0.3f, alpha * gridLine);

    if (yAxisWeight > 0.0f)
        finalColor = lerp(finalColor, float4(0.0f, 1.0f, 0.0f, alpha), yAxisWeight);
    if (xAxisWeight > 0.0f)
        finalColor = lerp(finalColor, float4(1.0f, 0.0f, 0.0f, alpha), xAxisWeight);

    return finalColor;
}