#pragma once

#include <d3d11.h>

#include "Core/Container/Array.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"

#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Renderer/Line/LineDrawRequest.h"
#include "Engine/Renderer/Text/WorldTextItem.h"
#include "Core/Math/Box.h"

class UCameraComponent;
class UPrimitiveComponent;
struct FStaticUniformGridCell;

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
    FBoundingBox WorldBounds{};
    bool bHasWorldBounds = false;

    //Impostor
    FVector ImpostorCenterWS{};
    FVector4 ImpostorSize{ 1.0f, 1.0f, 0.0f, 0.0f };
    FVector4 ImpostorUV{ 1.0f, 1.0f, 0.0f, 0.0f };
    FVector ImpostorCameraLocation{};
    const UPrimitiveComponent* SourcePrimitive = nullptr;
};

// 아직 Primitive를 만들지 않은 cell 후보
struct FGridCellCandidate
{
    uint64 Key = 0;
    FBoundingBox OcclusionBounds;
    const FStaticUniformGridCell* SourceCell = nullptr;  // 그릴 때 내부 Primitive에 접근하기 위한 원본 Grid cell
    bool bFullyInsideFrustum = false;
};

struct FViewRenderData
{
    FRenderViewSnapshot View;
    TArray<FRenderObjectData> Objects;
    TArray<FPrimitiveRenderData> Primitives;
    TArray<FPrimitiveRenderData> Gizmos;
    FLineDrawRequest Lines;
    const UCameraComponent* TextCamera = nullptr;
    TArray<FTextDrawRequest> TextRequests;
    TArray<FGridCellCandidate> GridCellCandidates;
    uint32 HZBRenderCellCount = 0;

    void Reset() {
        View = FRenderViewSnapshot{};
        Objects.Empty();
        Primitives.Empty();
        Gizmos.Empty();
        Lines.Vertices.Empty();
        Lines.Indices.Empty();
        TextCamera = nullptr;
        TextRequests.Empty();
        GridCellCandidates.Empty();
        HZBRenderCellCount = 0;
    }

};
