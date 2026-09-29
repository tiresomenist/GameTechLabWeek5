#pragma once

#include "Core/Container/Array.h"
#include "Core/Container/String.h"
#include "Core/Math/Box.h"
#include "Core/Math/Matrix.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Engine/Renderer/RenderConstants.h"
#include "Engine/Renderer/RenderDataTypes.h"


class FFontAtlas;
class UWidgetComponent;
class UTextComponent;

// 컴포넌트가 소유한다. 카메라에 무관한 로컬 글자 배치만 저장한다.
struct FTextLayoutCache
{
    FString Text;
    const FFontAtlas* Atlas = nullptr;
    uint64 AtlasRevision = 0;
    float WorldUnitsPerPixel = 0.02f;
    TArray<FVertexTexture> LocalVertices;
    FBoundingBox LocalBounds;
    bool bValid = false;
};

// 해당 View에서 즉시 소비하는 텍스트 데이터다.
struct FWorldTextItem
{
    FString Text;
    FMatrix WorldMatrix;
    const FTextLayoutCache* Layout = nullptr;
};

// 가시성 판정 전에 문자열과 정점을 만들지 않고 대상만 보관한다.
struct FTextDrawRequest
{
    const UWidgetComponent* Widget = nullptr;
    const UTextComponent* TextComponent = nullptr;
    uint32 OwnerObjectIndex = InvalidRenderId;
};