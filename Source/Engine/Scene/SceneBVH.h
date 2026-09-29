#pragma once

#include "Core/Container/Map.h"
#include "Core/Math/Vector.h"

class FSceneBVHNode;
class UStaticMeshComponent;

struct FSceneBVHNode final
{
    FVector WorldMin{};
    FVector WorldMax{};

    FSceneBVHNode* Parent = nullptr;
    FSceneBVHNode* LeftChild = nullptr;
    FSceneBVHNode* RightChild = nullptr;

    // 리프에서만 사용하며, 컴포넌트를 소유하지는 않는다.
    UStaticMeshComponent* ComponentOrNull = nullptr;
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

private:
    static bool CalculateWorldBounds(UStaticMeshComponent* Component, FVector& OutMin, FVector& OutMax);
    static bool Contains(const FSceneBVHNode* Node, const FVector& Min, const FVector& Max);

    static float GetJoinedBoundingBoxSurfaceArea(const FSceneBVHNode* First, const FSceneBVHNode* Second);

    void RefitParents(FSceneBVHNode* StartParentNode);
    void DeleteNodesRecursive(FSceneBVHNode* NodeOrNull);

    void Insert(FSceneBVHNode* Leaf);
    void Detach(FSceneBVHNode* Leaf);

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
