#pragma once

#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"

// HLSL의 float2 크기와 float2 오프셋에 대응하는 16바이트 상수
struct FTextureUVTransform
{
    FVector2 Scale{ 1.0f, 1.0f };
    FVector2 Offset{ 0.0f, 0.0f };

    FTextureUVTransform() = default;
    FTextureUVTransform(const FVector2& InScale, const FVector2& InOffset)
        : Scale(InScale), Offset(InOffset) {
    }
    FTextureUVTransform(float ScaleU, float ScaleV, float OffsetU, float OffsetV)
        : Scale(ScaleU, ScaleV), Offset(OffsetU, OffsetV) {
    }
};

// b0
struct FObjectConstants
{
    FMatrix World = FMatrix::Identity;
    FMatrix ViewProjection = FMatrix::Identity;
};

// b1
struct FTextureDrawConstants
{
    FTextureUVTransform UV;

    FVector4 DiffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float AlphaCutoff = 0.0f;

    float Padding[3]{};
};

static_assert(sizeof(FTextureUVTransform) == 16);
static_assert(sizeof(FObjectConstants) == 128);
static_assert(sizeof(FTextureDrawConstants) == 48);