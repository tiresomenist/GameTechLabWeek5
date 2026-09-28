#pragma once

#include "Core/Math/Vector.h"

class UStaticMeshComponent;

class FSceneBVHNode
{
public:
    // Leaf가 nullptr이면 새 리프를 만든다.
    // 기존 Fat AABB를 벗어나면 같은 리프를 분리한 뒤 다시 삽입한다.
    static FSceneBVHNode* Update(FSceneBVHNode*& Root, FSceneBVHNode* Leaf, UStaticMeshComponent* Component);
    static void Remove(FSceneBVHNode*& Root, FSceneBVHNode* Leaf);
    static void Clear(FSceneBVHNode*& Root);

    const FVector& GetMin() const { return Min; }
    const FVector& GetMax() const { return Max; }
    const FSceneBVHNode* GetLeft() const { return Left; }
    const FSceneBVHNode* GetRight() const { return Right; }
    UStaticMeshComponent* GetComponent() const { return Component; }

private:
    static bool CalculateWorldBounds(UStaticMeshComponent* Component, FVector& OutMin, FVector& OutMax);
    static bool Contains(const FSceneBVHNode* Node, const FVector& Min, const FVector& Max);
    static float JoinedArea(const FSceneBVHNode* A, const FSceneBVHNode* B);

    static void RefitParents(FSceneBVHNode* Node);
    static void Insert(FSceneBVHNode*& Root, FSceneBVHNode* Leaf);
    static void Detach(FSceneBVHNode*& Root, FSceneBVHNode* Leaf);

    FVector Min; // Fat AABB
    FVector Max;

    FSceneBVHNode* Parent = nullptr;
    FSceneBVHNode* Left = nullptr;
    FSceneBVHNode* Right = nullptr;

    // 리프에서만 사용한다.
    UStaticMeshComponent* Component = nullptr;
};