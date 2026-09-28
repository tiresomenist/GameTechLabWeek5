#pragma once

#include <d3d11.h>

#include "Core/Container/Array.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"

#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Renderer/Line/LineDrawRequest.h"
#include "Engine/Renderer/Text/WorldTextItem.h"

struct FRenderViewSnapshot
{
    FMatrix ViewMatrix = FMatrix::Identity;
    FMatrix ViewProjection = FMatrix::Identity;
    D3D11_VIEWPORT Viewport{};
    EViewModeIndex ViewMode = EViewModeIndex::VMI_Unlit;
};


struct FRenderObjectData
{
    FMatrix World = FMatrix::Identity;
    FVector SortCenterWS{};
};

struct FViewRenderData
{
    FRenderViewSnapshot View;
    TArray<FRenderObjectData> Objects;
    TArray<FPrimitiveRenderData> Primitives;
    TArray<FPrimitiveRenderData> Gizmos;
    FLineDrawRequest Lines;
    TArray<FWorldTextItem> TextItems;

    void Reset() {
        View = FRenderViewSnapshot{};
        Objects.Empty();
        Primitives.Empty();
        Gizmos.Empty();
        Lines.Vertices.Empty();
        Lines.Indices.Empty();
        TextItems.Empty();
    }

};