#pragma once

#include "Core/Math/Vector.h"
#include "Core/Container/Array.h"
#include "Engine/Renderer/VertexSimple.h"


class FGeometryGenerator
{
public:
    static void CreateTriangle(
        float Width,
        float Height,
        TArray<FVertexTexture>& OutVertices,
        TArray<uint32>& OutIndices
    );

    static void CreatePlane(
        float Width,
        float Height,
        uint32 SubdivisionsX,
        uint32 SubdivisionsY,
        TArray<FVertexTexture>& OutVertices,
        TArray<uint32>& OutIndices
    );

    static void CreateCube(
        float Width,
        float Height,
        float Depth,
        TArray<FVertexTexture>& OutVertices,
        TArray<uint32>& OutIndices
    );

    static void CreateSphere(
        float Radius,
        uint32 SliceCount,
        uint32 StackCount,
        TArray<FVertexTexture>& OutVertices,
        TArray<uint32>& OutIndices
    );
};