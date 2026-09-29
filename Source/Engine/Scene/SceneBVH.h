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

    FSceneBVH(const FSceneBVH&) = delete;
    FSceneBVH& operator=(const FSceneBVH&) = delete;

    void Update(UStaticMeshComponent* Component);
    void Remove(UStaticMeshComponent* Component);
    void Clear();

    const FSceneBVHNode* GetRoot() const { return RootOrNull; }

private:
    static bool CalculateWorldBounds(UStaticMeshComponent* Component, FVector& OutMin, FVector& OutMax);
    static bool Contains(const FSceneBVHNode* Node, const FVector& Min, const FVector& Max);

    static float GetJoinedBoundingBoxSurfaceArea(const FSceneBVHNode* A, const FSceneBVHNode* B);

    void RefitParents(FSceneBVHNode* StartParentNode);
    static void DeleteSubtree(FSceneBVHNode* Node);

    void Insert(FSceneBVHNode* Leaf);
    void Detach(FSceneBVHNode* Leaf);

private:
    static constexpr float FAT_LENGTH = 0.25f;

    FSceneBVHNode* RootOrNull = nullptr;
    TMap<UStaticMeshComponent*, FSceneBVHNode*> Leaves;
};