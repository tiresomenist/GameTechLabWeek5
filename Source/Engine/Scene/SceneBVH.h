#pragma once

#include "Core/Container/Array.h"
#include "Core/Container/Map.h"
#include "Core/Math/Vector.h"

class UStaticMeshComponent;

struct FSceneBVHNode
{
    FVector WorldMin{};
    FVector WorldMax{};

    FSceneBVHNode* Parent = nullptr;
    FSceneBVHNode* LeftChild = nullptr;
    FSceneBVHNode* RightChild = nullptr;

    // 리프에서만 사용하며, 컴포넌트를 소유하지는 않는다.
    UStaticMeshComponent* ComponentOrNull = nullptr;
};

// Todo: Change name
// 일괄 빌드 중에만 사용하는 객체의 월드 공간 정보
struct FBuildItem
{
    FVector BoundsMin{};
    FVector BoundsMax{};
    FVector Centroid{};

    UStaticMeshComponent* Component = nullptr;
};

// 각 분할 위치의 오른쪽 그룹 AABB를 빠르게 조회하기 위한 임시 배열이다.
struct FBounds
{
    FVector Min{};
    FVector Max{};
};

class FSceneBVH final
{
public:
    FSceneBVH() = default;
    ~FSceneBVH();

    FSceneBVH(const FSceneBVH& other) = delete;
    FSceneBVH& operator=(const FSceneBVH& other) = delete;

    void Update(UStaticMeshComponent* Component);
    void Remove(UStaticMeshComponent* Component);
    void Clear();

    const FSceneBVHNode* GetRoot() const;

    void Build(const TArray<UStaticMeshComponent*>& Components);

private:
    bool CalculateWorldBounds(UStaticMeshComponent* Component, FVector& OutMin, FVector& OutMax);
    bool Contains(const FSceneBVHNode* Node, const FVector& Min, const FVector& Max);

    float GetJoinedBoundingBoxSurfaceArea(const FSceneBVHNode* First, const FSceneBVHNode* Second);

    void RefitParents(FSceneBVHNode* StartParentNode);
    void DeleteNodesRecursive(FSceneBVHNode* NodeOrNull);

    void Insert(FSceneBVHNode* Leaf);
    void Detach(FSceneBVHNode* Leaf);

    FSceneBVHNode* BuildNodesRecursive(TArray<FBuildItem>& Items, uint32 First, uint32 Count);

private:
    static constexpr float FAT_LENGTH = 0.25f;

    // Todo:
    // 메시 추가 시 맵에 추가하지말고,
    // 미리 만들어놓은 후 맵에 넣고, 트리만 만들기
    FSceneBVHNode* RootOrNull = nullptr;
    TMap<UStaticMeshComponent*, FSceneBVHNode*> LeafNodeMap;
};

inline const FSceneBVHNode* FSceneBVH::GetRoot() const
{ 
    return RootOrNull; 
}
