#pragma once
// Todo: BVH Mesh
#include "Core/Core.h"
#include "Core/Math/Vector.h"

#include "Core/Container/Array.h"

struct FMeshBVHNode final
{
    // 이 노드에 속한 삼각형들을 모두 감싸는 로컬 AABB
    FVector LocalMin{};
    FVector LocalMax{};

    // 내부 노드에서 사용하는 자식
    FMeshBVHNode* LeftChild = nullptr;
    FMeshBVHNode* RightChild = nullptr;

    // 리프에서 사용하는 피킹 삼각형 배열의 범위
    uint32 FirstTriangleOffset = 0;
    uint32 TrianglesCount = 0;
};

struct FMeshTriangleInfo
{
    uint32 TriangleIndex = 0;
    FVector BoundsMin{};
    FVector BoundsMax{};
    FVector Centroid{};
};

// BVH 리프 순서로 저장해 피킹 시 인덱스 조회와 변 계산을 반복하지 않는다.
struct FMeshPickTriangle
{
    FVector A{};
    FVector Edge1{};
    FVector Edge2{};
};

class FMeshBVH final
{
public:
    FMeshBVH() = default;
    ~FMeshBVH();

    FMeshBVH(const FMeshBVH&) = delete;
    FMeshBVH& operator=(const FMeshBVH&) = delete;

    void Build(const TArray<FVector>& Positions, const TArray<uint32>& Indices);
    const FMeshBVHNode* GetRoot() const;
    const TArray<FMeshPickTriangle>& GetPickTriangles() const;
    
private:
    void Clear();
    FMeshBVHNode* BuildNodesIterative(TArray<FMeshTriangleInfo>& TriangleInfos, uint32 StartIndex, uint32 Count);

private:
    static const uint32 MAX_TRIANGLES_PER_LEAF = 6;

    FMeshBVHNode* RootOrNull = nullptr;

    TArray<FMeshBVHNode*> AllocatedNodes;
    TArray<FMeshPickTriangle> PickTriangles;
};

inline const FMeshBVHNode* FMeshBVH::GetRoot() const
{
    return RootOrNull;
}

inline const TArray<FMeshPickTriangle>& FMeshBVH::GetPickTriangles() const
{
    return PickTriangles;
}
