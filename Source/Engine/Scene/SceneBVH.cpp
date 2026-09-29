#include "pch.h"
#include "SceneBVH.h"

#include "Core/Math/Matrix.h"
#include "Engine/Component/StaticMeshComponent.h"

#include <algorithm>
#include <cassert>

FSceneBVH::~FSceneBVH()
{
    Clear();
}

void FSceneBVH::Update(UStaticMeshComponent* Component)
{
    if (!Component)
    {
        return;
    }

    FVector NewWorldMin;
    FVector NewWorldMax;

    if (CalculateWorldBounds(Component, NewWorldMin, NewWorldMax) == false)
    {
        Remove(Component);

        return;
    }

    FSceneBVHNode** FoundNodeOrNull = Leaves.Find(Component);
    FSceneBVHNode* LeafOrNull = (FoundNodeOrNull != nullptr) ? *FoundNodeOrNull : nullptr;

    if (LeafOrNull)
    {
        // 실제 AABB가 기존 Fat AABB 안에 있으면 트리를 유지한다.
        if (Contains(LeafOrNull, NewWorldMin, NewWorldMax))
        {
            return;
        }

        Detach(LeafOrNull);
    }
    else
    {
        LeafOrNull = new FSceneBVHNode;
        LeafOrNull->ComponentOrNull = Component;
    }

    LeafOrNull->WorldMin = NewWorldMin - FAT_LENGTH;
    LeafOrNull->WorldMax = NewWorldMax + FAT_LENGTH;

    Insert(LeafOrNull);

    if (FoundNodeOrNull == nullptr)
    {
        Leaves.Add(Component, LeafOrNull);
    }
}

void FSceneBVH::Remove(UStaticMeshComponent* Component)
{
    FSceneBVHNode** FoundNodeOrNull = Leaves.Find(Component);
    assert(FoundNodeOrNull != nullptr);
    
    FSceneBVHNode* Leaf = *FoundNodeOrNull;
    Detach(Leaf);
    Leaves.Remove(Component);

    delete Leaf;
}

void FSceneBVH::Clear()
{
    DeleteSubtree(RootOrNull);

    RootOrNull = nullptr;
    Leaves.Empty();
}

bool FSceneBVH::CalculateWorldBounds(UStaticMeshComponent* Component, FVector& OutMin, FVector& OutMax)
{
    FVector LocalMin;
    FVector LocalMax;

    if (!Component->GetLocalBounds(LocalMin, LocalMax))
    {
        return false;
    }

    const FMatrix& World = Component->GetWorldMatrix();

    // 회전과 음수 스케일을 포함해 로컬 AABB의 모서리 8개를 변환한다.
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        const FVector Local(
            (Corner & 1) ? LocalMax.X : LocalMin.X,
            (Corner & 2) ? LocalMax.Y : LocalMin.Y,
            (Corner & 4) ? LocalMax.Z : LocalMin.Z);

        const FVector Position = World.TransformPosition(Local);

        if (Corner == 0)
        {
            OutMin = Position;
            OutMax = Position;
            continue;
        }

        OutMin.X = (std::min)(OutMin.X, Position.X);
        OutMin.Y = (std::min)(OutMin.Y, Position.Y);
        OutMin.Z = (std::min)(OutMin.Z, Position.Z);

        OutMax.X = (std::max)(OutMax.X, Position.X);
        OutMax.Y = (std::max)(OutMax.Y, Position.Y);
        OutMax.Z = (std::max)(OutMax.Z, Position.Z);
    }

    return true;
}

bool FSceneBVH::Contains(const FSceneBVHNode* Node, const FVector& WorldMin, const FVector& WorldMax)
{
    return WorldMin.X >= Node->WorldMin.X && WorldMax.X <= Node->WorldMax.X
        && WorldMin.Y >= Node->WorldMin.Y && WorldMax.Y <= Node->WorldMax.Y
        && WorldMin.Z >= Node->WorldMin.Z && WorldMax.Z <= Node->WorldMax.Z;
}

float FSceneBVH::GetJoinedBoundingBoxSurfaceArea(const FSceneBVHNode* A, const FSceneBVHNode* B)
{
    const FVector Min(
        std::min(A->WorldMin.X, B->WorldMin.X),
        std::min(A->WorldMin.Y, B->WorldMin.Y),
        std::min(A->WorldMin.Z, B->WorldMin.Z));

    const FVector Max(
        std::max(A->WorldMax.X, B->WorldMax.X),
        std::max(A->WorldMax.Y, B->WorldMax.Y),
        std::max(A->WorldMax.Z, B->WorldMax.Z));

    // Return bounding box surface area
    const FVector BoundingBoxLength = Max - Min;
    return 2.f * (BoundingBoxLength.X * BoundingBoxLength.Y + BoundingBoxLength.Y * BoundingBoxLength.Z + BoundingBoxLength.Z * BoundingBoxLength.X);
}

void FSceneBVH::RefitParents(FSceneBVHNode* StartParentNode)
{
    FSceneBVHNode* NodeOrNull = StartParentNode;

    while (NodeOrNull != nullptr)
    {
        assert(NodeOrNull->LeftChild && NodeOrNull->RightChild);

        NodeOrNull->WorldMin = FVector(
            std::min(NodeOrNull->LeftChild->WorldMin.X, NodeOrNull->RightChild->WorldMin.X),
            std::min(NodeOrNull->LeftChild->WorldMin.Y, NodeOrNull->RightChild->WorldMin.Y),
            std::min(NodeOrNull->LeftChild->WorldMin.Z, NodeOrNull->RightChild->WorldMin.Z));

        NodeOrNull->WorldMax = FVector(
            std::max(NodeOrNull->LeftChild->WorldMax.X, NodeOrNull->RightChild->WorldMax.X),
            std::max(NodeOrNull->LeftChild->WorldMax.Y, NodeOrNull->RightChild->WorldMax.Y),
            std::max(NodeOrNull->LeftChild->WorldMax.Z, NodeOrNull->RightChild->WorldMax.Z));

        NodeOrNull = NodeOrNull->Parent;
    }
}

void FSceneBVH::Insert(FSceneBVHNode* NewLeaf)
{
    if (!RootOrNull)
    {
        RootOrNull = NewLeaf;
        NewLeaf->Parent = nullptr;

        return;
    }

    FSceneBVHNode* LessAreaCandidate = RootOrNull;

    // 새 리프와 합친 AABB의 표면적이 작은 자식을 따라간다.
    while (LessAreaCandidate->ComponentOrNull == nullptr)
    {
        const float LeftChildJoinedArea = GetJoinedBoundingBoxSurfaceArea(LessAreaCandidate->LeftChild, NewLeaf);
        const float RightChildJoinedArea = GetJoinedBoundingBoxSurfaceArea(LessAreaCandidate->RightChild, NewLeaf);

        LessAreaCandidate = (LeftChildJoinedArea <= RightChildJoinedArea) ? LessAreaCandidate->LeftChild : LessAreaCandidate->RightChild;
    }

    FSceneBVHNode* OldParent = LessAreaCandidate->Parent;
    FSceneBVHNode* NewParent = new FSceneBVHNode;

    NewParent->Parent = OldParent;
    NewParent->LeftChild = LessAreaCandidate;
    NewParent->RightChild = NewLeaf;

    LessAreaCandidate->Parent = NewParent;
    NewLeaf->Parent = NewParent;


    // Todo: Check
    if (OldParent)
    {
        if (OldParent->LeftChild == LessAreaCandidate)
        {
            OldParent->LeftChild = NewParent;
        }
        else
        {
            OldParent->RightChild = NewParent;
        }
    }
    else
    {
        RootOrNull = NewParent;
    }

    RefitParents(NewParent);
}

void FSceneBVH::Detach(FSceneBVHNode* Leaf)
{
    if (Leaf == RootOrNull)
    {
        RootOrNull = nullptr;
        Leaf->Parent = nullptr;
        return;
    }

    FSceneBVHNode* Parent = Leaf->Parent;
    FSceneBVHNode* Sibling =
        (Parent->LeftChild == Leaf) ? Parent->RightChild : Parent->LeftChild;
    FSceneBVHNode* Grandparent = Parent->Parent;

    if (Grandparent)
    {
        if (Grandparent->LeftChild == Parent)
            Grandparent->LeftChild = Sibling;
        else
            Grandparent->RightChild = Sibling;

        Sibling->Parent = Grandparent;
    }
    else
    {
        RootOrNull = Sibling;
        Sibling->Parent = nullptr;
    }

    Leaf->Parent = nullptr;
    delete Parent;

    RefitParents(Grandparent);
}

void FSceneBVH::DeleteSubtree(FSceneBVHNode* Node)
{
    if (!Node)
    {
        return;
    }

    DeleteSubtree(Node->LeftChild);
    DeleteSubtree(Node->RightChild);

    delete Node;
}