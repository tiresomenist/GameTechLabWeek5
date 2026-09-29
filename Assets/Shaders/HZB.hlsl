
Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> OutputHiZ : register(u0);

cbuffer HZBConstants : register(b0)
{
    uint2 SourceSize;
    uint SourceMip;
    uint Padding;
};

// CopyDepthCS
// 현재 depth buffer -> HZB mip 0으로 복사
[numthreads(8, 8, 1)]
void CopyDepthCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= SourceSize.x || DispatchThreadID.y >= SourceSize.y)
    {
        return;
    }
    OutputHiZ[DispatchThreadID.xy] = SourceDepth.Load(int3(DispatchThreadID.xy, 0));
}

// DownsampleMaxCS
// mip N+1 생성
[numthreads(8, 8, 1)]
void DownsampleMaxCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint2 DestinationSize = max(uint2(1, 1), SourceSize / 2);
    if (DispatchThreadID.x >= DestinationSize.x || DispatchThreadID.y >= DestinationSize.y)
    {
        return;
    }
    const uint2 SourceCoordinate = DispatchThreadID.xy * 2;
    const uint2 LastSourceCoordinate = SourceSize - uint2(1, 1);
    const float Depth00 = SourceDepth.Load(int3(min(SourceCoordinate, LastSourceCoordinate), 0));
    const float Depth10 = SourceDepth.Load(int3(min(SourceCoordinate + uint2(1, 0), LastSourceCoordinate), 0));
    const float Depth01 = SourceDepth.Load(int3(min(SourceCoordinate + uint2(0, 1), LastSourceCoordinate), 0));
    const float Depth11 = SourceDepth.Load(int3(min(SourceCoordinate + uint2(1, 1), LastSourceCoordinate), 0));
    OutputHiZ[DispatchThreadID.xy] = max(max(Depth00, Depth10), max(Depth01, Depth11));
}

struct FHZBCellData
{
    float4 BoundsMin;
    float4 BoundsMax;
};

StructuredBuffer<FHZBCellData> Cells : register(t1);
Texture2D<float> HierarchicalZ : register(t2);
RWStructuredBuffer<uint> CellVisibility : register(u1);
cbuffer HZBCullConstants : register(b1)
{
    row_major float4x4 ViewProjection;
    uint CellCount;
    float ViewportWidth;
    float ViewportHeight;
    uint HZBMipCount;
    float ViewportTopLeftX;
    float ViewportTopLeftY;
    uint Padding0;
    uint Padding1;
};

// CullCellsCS
// 완성된 HZB를 읽고 cell이 가려졌는지 검사
[numthreads(64, 1, 1)]
void CullCellsCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint CellIndex = DispatchThreadID.x;
    if (CellIndex >= CellCount)
    {
        return;
    }
    
    const float3 BoundsMin = Cells[CellIndex].BoundsMin.xyz;
    const float3 BoundsMax = Cells[CellIndex].BoundsMax.xyz;
    const float3 Corners[8] =
    {
        float3(BoundsMin.x, BoundsMin.y, BoundsMin.z),
		float3(BoundsMax.x, BoundsMin.y, BoundsMin.z),
		float3(BoundsMin.x, BoundsMax.y, BoundsMin.z),
		float3(BoundsMax.x, BoundsMax.y, BoundsMin.z),
		float3(BoundsMin.x, BoundsMin.y, BoundsMax.z),
		float3(BoundsMax.x, BoundsMin.y, BoundsMax.z),
		float3(BoundsMin.x, BoundsMax.y, BoundsMax.z),
		float3(BoundsMax.x, BoundsMax.y, BoundsMax.z)
    };
    
    float2 NdcMin = float2(1.0f, 1.0f);
    float2 NdcMax = float2(-1.0f, -1.0f);
    float NearestDepth = 1.0f;
    
	[unroll]  // 반복문 전개
    for (uint CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
    {
        const float4 ClipPosition = mul(float4(Corners[CornerIndex], 1.0f), ViewProjection);
        if (ClipPosition.w <= 0.0001f)
        {
            CellVisibility[CellIndex] = 1;
            return;
        }
        const float3 NdcPosition = ClipPosition.xyz / ClipPosition.w;
        NdcMin = min(NdcMin, NdcPosition.xy);
        NdcMax = max(NdcMax, NdcPosition.xy);
        NearestDepth = min(NearestDepth, NdcPosition.z);
    }
    
    if (NdcMax.x < -1.0f || NdcMin.x > 1.0f || NdcMax.y < -1.0f || NdcMin.y > 1.0f)
    {
        CellVisibility[CellIndex] = 1;
        return;
    }
    
    NdcMin = max(NdcMin, float2(-1.0f, -1.0f));
    NdcMax = min(NdcMax, float2(1.0f, 1.0f));
    const float2 ViewportOffset = float2(ViewportTopLeftX, ViewportTopLeftY);
    const float2 ScreenMin = ViewportOffset + float2((NdcMin.x + 1.0f) * 0.5f * ViewportWidth, (1.0f - NdcMax.y) * 0.5f * ViewportHeight);
    const float2 ScreenMax = ViewportOffset + float2((NdcMax.x + 1.0f) * 0.5f * ViewportWidth, (1.0f - NdcMin.y) * 0.5f * ViewportHeight);
    const float LargestExtent = max(max(ScreenMax.x - ScreenMin.x, ScreenMax.y - ScreenMin.y), 1.0f);
    
    const uint MipLevel = min((uint)floor(log2(LargestExtent)), HZBMipCount - 1u);
    uint MipWidth;
    uint MipHeight;
    uint MipLevels;
    
    HierarchicalZ.GetDimensions(MipLevel, MipWidth, MipHeight, MipLevels);
    const float MipScale = exp2(float(MipLevel));
    const uint2 LastMipCoordinate = uint2(MipWidth - 1, MipHeight - 1);
    const uint2 MipMin = min(uint2(ScreenMin / MipScale), LastMipCoordinate);
    const uint2 MipMax = min(uint2(ScreenMax / MipScale), LastMipCoordinate);
    
    const float Depth00 = HierarchicalZ.Load(int3(MipMin, MipLevel));
    const float Depth10 = HierarchicalZ.Load(int3(uint2(MipMax.x, MipMin.y), MipLevel));
    const float Depth01 = HierarchicalZ.Load(int3(uint2(MipMin.x, MipMax.y), MipLevel));
    const float Depth11 = HierarchicalZ.Load(int3(MipMax, MipLevel));
    const float DepthBias = 0.0001f;
    const bool bOccluded = NearestDepth > Depth00 + DepthBias && NearestDepth > Depth10 + DepthBias 
        && NearestDepth > Depth01 + DepthBias && NearestDepth > Depth11 + DepthBias;
    
    CellVisibility[CellIndex] = bOccluded ? 0 : 1;

    
}
