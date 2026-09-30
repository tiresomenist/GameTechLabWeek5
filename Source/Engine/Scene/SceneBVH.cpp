#include "pch.h"
#include "SceneBVH.h"

#include "Core/Math/Matrix.h"
#include "Engine/Component/StaticMeshComponent.h"

#include <algorithm>
#include <cassert>

#include <limits>

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

    FSceneBVHNode** FoundNodeOrNull = LeafNodeMap.Find(Component);
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
        LeafNodeMap.Add(Component, LeafOrNull);
    }
}

void FSceneBVH::Remove(UStaticMeshComponent* Component)
{
    /*
    FSceneBVHNode** FoundNodeOrNull = LeafNodeMap.Find(Component);
    if (!FoundNodeOrNull)return;
    
    FSceneBVHNode* Leaf = *FoundNodeOrNull;
    Detach(Leaf);
    LeafNodeMap.Remove(Component);

    delete Leaf;
    */

    FSceneBVHNode** FoundNodeOrNull = LeafNodeMap.Find(Component);

    // 유효한 AABB가 없어 트리에 등록되지 않은 컴포넌트도 있을 수 있다.
    if (FoundNodeOrNull == nullptr)
    {
        return;
    }

    FSceneBVHNode* Leaf = *FoundNodeOrNull;
    Detach(Leaf);
    LeafNodeMap.Remove(Component);

    delete Leaf;
}

void FSceneBVH::Clear()
{
    DeleteNodesRecursive(RootOrNull);
    RootOrNull = nullptr;

    LeafNodeMap.Empty();
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

float FSceneBVH::GetJoinedBoundingBoxSurfaceArea(const FSceneBVHNode* First, const FSceneBVHNode* Second)
{
    const FVector Min(
        std::min(First->WorldMin.X, Second->WorldMin.X),
        std::min(First->WorldMin.Y, Second->WorldMin.Y),
        std::min(First->WorldMin.Z, Second->WorldMin.Z));

    const FVector Max(
        std::max(First->WorldMax.X, Second->WorldMax.X),
        std::max(First->WorldMax.Y, Second->WorldMax.Y),
        std::max(First->WorldMax.Z, Second->WorldMax.Z));

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

void FSceneBVH::Detach(FSceneBVHNode* DetachLeaf)
{
    if (DetachLeaf == RootOrNull)
    {
        RootOrNull = nullptr;
        DetachLeaf->Parent = nullptr;

        return;
    }

    FSceneBVHNode* Parent = DetachLeaf->Parent;
    FSceneBVHNode* Sibling = (Parent->LeftChild == DetachLeaf) ? Parent->RightChild : Parent->LeftChild;
    FSceneBVHNode* GrandparentOrNull = Parent->Parent;

    if (GrandparentOrNull != nullptr)
    {
        if (GrandparentOrNull->LeftChild == Parent)
        {
            GrandparentOrNull->LeftChild = Sibling;
        }
        else
        {
            GrandparentOrNull->RightChild = Sibling;
        }

        Sibling->Parent = GrandparentOrNull;
    }
    else
    {
        RootOrNull = Sibling;
        Sibling->Parent = nullptr;
    }

    DetachLeaf->Parent = nullptr;
    delete Parent;

    RefitParents(GrandparentOrNull);
}

void FSceneBVH::DeleteNodesRecursive(FSceneBVHNode* NodeOrNull)
{
    if (NodeOrNull == nullptr)
    {
        return;
    }

    DeleteNodesRecursive(NodeOrNull->LeftChild);
    DeleteNodesRecursive(NodeOrNull->RightChild);

    delete NodeOrNull;
}

void FSceneBVH::Build(const TArray<UStaticMeshComponent*>& Components)
{
    Clear();

    TArray<FBuildItem> Items;
    Items.Reserve(Components.Num());

    // 각 객체의 월드 AABB와 중심점을 수집한다.
    for (UStaticMeshComponent* Component : Components)
    {
        if (Component == nullptr)
        {
            continue;
        }

        FVector WorldMin;
        FVector WorldMax;

        if (!CalculateWorldBounds(Component, WorldMin, WorldMax))
        {
            continue;
        }

        FBuildItem Item;
        Item.Component = Component;
        Item.BoundsMin = WorldMin;
        Item.BoundsMax = WorldMax;
        Item.Centroid = (WorldMin + WorldMax) / 2.0f;

        Items.Add(Item);
    }

    // 유효한 객체가 없으면 빈 트리로 둔다.
    if (Items.IsEmpty())
    {
        return;
    }

    // 수집한 객체 전체를 한 번에 분할해 트리를 만든다.
    RootOrNull = BuildNodesRecursive(Items, 0, static_cast<uint32>(Items.Num()));
}

FSceneBVHNode* FSceneBVH::BuildNodesRecursive(TArray<FBuildItem>& Items, uint32 First, uint32 Count)
{
    assert(Count > 0);
    assert(First + Count <= static_cast<uint32>(Items.Num()));

    FSceneBVHNode* Node = new FSceneBVHNode;

    // 객체가 하나면 리프 노드를 만들고, 이후 갱신과 삭제를 위해 맵에 등록한다.
    if (Count == 1)
    {
        const FBuildItem& Item = Items[First];

        Node->WorldMin = Item.BoundsMin;
        Node->WorldMax = Item.BoundsMax;
        Node->ComponentOrNull = Item.Component;

        LeafNodeMap.Add(Item.Component, Node);
        return Node;
    }

    float BestCost = (std::numeric_limits<float>::max)();
    int BestAxis = -1;
    uint32 BestLeftCount = 0;
    uint32 BestImbalance = Count;

    // X, Y, Z축에서 가능한 분할 위치를 모두 평가한다.
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        // 현재 노드가 맡은 범위만 중심점 순서로 정렬한다.
        std::sort(Items.begin() + First, Items.begin() + First + Count,
            [Axis](const FBuildItem& A, const FBuildItem& B)
            {
                return A.Centroid[Axis] < B.Centroid[Axis];
            });

        TArray<FBounds> SuffixBounds;
        SuffixBounds.SetNum(Count);

        // 각 위치부터 범위 끝까지의 AABB를 뒤에서부터 계산한다.
        {
            const FBuildItem& LastItem = Items[First + Count - 1];
            SuffixBounds[Count - 1].Min = LastItem.BoundsMin;
            SuffixBounds[Count - 1].Max = LastItem.BoundsMax;

            for (uint32 Index = Count - 1; Index > 0; --Index)
            {
                const FBuildItem& Item = Items[First + Index - 1];
                const FBounds& Next = SuffixBounds[Index];
                FBounds& Current = SuffixBounds[Index - 1];

                Current.Min.X = (std::min)(Item.BoundsMin.X, Next.Min.X);
                Current.Min.Y = (std::min)(Item.BoundsMin.Y, Next.Min.Y);
                Current.Min.Z = (std::min)(Item.BoundsMin.Z, Next.Min.Z);

                Current.Max.X = (std::max)(Item.BoundsMax.X, Next.Max.X);
                Current.Max.Y = (std::max)(Item.BoundsMax.Y, Next.Max.Y);
                Current.Max.Z = (std::max)(Item.BoundsMax.Z, Next.Max.Z);
            }
        }

        FVector LeftMin = Items[First].BoundsMin;
        FVector LeftMax = Items[First].BoundsMax;

        // 왼쪽 그룹을 하나씩 늘리며 모든 분할 위치의 SAH 비용을 구한다.
        for (uint32 LeftCount = 1; LeftCount < Count; ++LeftCount)
        {
            if (LeftCount > 1)
            {
                const FBuildItem& AddedItem = Items[First + LeftCount - 1];

                LeftMin.X = (std::min)(LeftMin.X, AddedItem.BoundsMin.X);
                LeftMin.Y = (std::min)(LeftMin.Y, AddedItem.BoundsMin.Y);
                LeftMin.Z = (std::min)(LeftMin.Z, AddedItem.BoundsMin.Z);

                LeftMax.X = (std::max)(LeftMax.X, AddedItem.BoundsMax.X);
                LeftMax.Y = (std::max)(LeftMax.Y, AddedItem.BoundsMax.Y);
                LeftMax.Z = (std::max)(LeftMax.Z, AddedItem.BoundsMax.Z);
            }

            // 중심 좌표가 같은 객체들 사이에서는 공간 분할을 하지 않는다.
            if (Items[First + LeftCount - 1].Centroid[Axis] == Items[First + LeftCount].Centroid[Axis])
            {
                continue;
            }

            const uint32 RightCount = Count - LeftCount;
            const FBounds& RightBounds = SuffixBounds[LeftCount];

            const FVector LeftSize = LeftMax - LeftMin;
            const FVector RightSize = RightBounds.Max - RightBounds.Min;

            const float LeftArea = 2.0f * (LeftSize.X * LeftSize.Y + LeftSize.Y * LeftSize.Z + LeftSize.Z * LeftSize.X);
            const float RightArea = 2.0f * (RightSize.X * RightSize.Y + RightSize.Y * RightSize.Z + RightSize.Z * RightSize.X);

            const float Cost = LeftArea * static_cast<float>(LeftCount) + RightArea * static_cast<float>(RightCount);
            const uint32 Imbalance = (LeftCount > RightCount) ? LeftCount - RightCount : RightCount - LeftCount;

            // 비용이 같으면 객체 수가 더 균형 잡힌 분할을 선택한다.
            if (Cost < BestCost || (Cost == BestCost && Imbalance < BestImbalance))
            {
                BestCost = Cost;
                BestAxis = Axis;
                BestLeftCount = LeftCount;
                BestImbalance = Imbalance;
            }
        }
    }

    // 모든 객체의 중심점이 같으면 개수를 절반으로 나눠 재귀를 진행한다.
    if (BestAxis < 0)
    {
        BestAxis = 0;
        BestLeftCount = Count / 2;
    }

    // 최종 선택한 축으로 다시 정렬해 왼쪽과 오른쪽 범위를 확정한다.
    std::sort(Items.begin() + First, Items.begin() + First + Count,
        [BestAxis](const FBuildItem& A, const FBuildItem& B)
        {
            return A.Centroid[BestAxis] < B.Centroid[BestAxis];
        });

    // 두 그룹의 자식 노드를 만든다.
    {
        Node->LeftChild = BuildNodesRecursive(Items, First, BestLeftCount);
        Node->RightChild = BuildNodesRecursive(Items, First + BestLeftCount, Count - BestLeftCount);

        Node->LeftChild->Parent = Node;
        Node->RightChild->Parent = Node;
    }

    // 자식 AABB를 합쳐 현재 내부 노드의 AABB를 만든다.
    {
        const FSceneBVHNode* Left = Node->LeftChild;
        const FSceneBVHNode* Right = Node->RightChild;

        Node->WorldMin.X = (std::min)(Left->WorldMin.X, Right->WorldMin.X);
        Node->WorldMin.Y = (std::min)(Left->WorldMin.Y, Right->WorldMin.Y);
        Node->WorldMin.Z = (std::min)(Left->WorldMin.Z, Right->WorldMin.Z);

        Node->WorldMax.X = (std::max)(Left->WorldMax.X, Right->WorldMax.X);
        Node->WorldMax.Y = (std::max)(Left->WorldMax.Y, Right->WorldMax.Y);
        Node->WorldMax.Z = (std::max)(Left->WorldMax.Z, Right->WorldMax.Z);
    }

    return Node;
}