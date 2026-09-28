#include "pch.h"

#include "SceneBVHNode.h"

#include "Core/Math/Matrix.h"
#include "Engine/Component/StaticMeshComponent.h"

#include <algorithm>

bool FSceneBVHNode::CalculateWorldBounds(
    UStaticMeshComponent* Component,
    FVector& OutMin,
    FVector& OutMax)
{
    FVector LocalMin;
    FVector LocalMax;
    if (!Component->GetLocalBounds(LocalMin, LocalMax))
        return false;

    const FMatrix& World = Component->GetWorldMatrix();

    // 회전과 음수 스케일까지 반영하기 위해 8개 모서리를 변환한다.
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

bool FSceneBVHNode::Contains(
    const FSceneBVHNode* Node,
    const FVector& Min,
    const FVector& Max)
{
    return Min.X >= Node->Min.X && Max.X <= Node->Max.X &&
        Min.Y >= Node->Min.Y && Max.Y <= Node->Max.Y &&
        Min.Z >= Node->Min.Z && Max.Z <= Node->Max.Z;
}

float FSceneBVHNode::JoinedArea(
    const FSceneBVHNode* A,
    const FSceneBVHNode* B)
{
    const FVector Min(
        (std::min)(A->Min.X, B->Min.X),
        (std::min)(A->Min.Y, B->Min.Y),
        (std::min)(A->Min.Z, B->Min.Z));

    const FVector Max(
        (std::max)(A->Max.X, B->Max.X),
        (std::max)(A->Max.Y, B->Max.Y),
        (std::max)(A->Max.Z, B->Max.Z));

    const FVector Size = Max - Min;
    return 2.0f * (
        Size.X * Size.Y +
        Size.Y * Size.Z +
        Size.Z * Size.X);
}

void FSceneBVHNode::RefitParents(FSceneBVHNode* Node)
{
    while (Node)
    {
        if (Node->Left && Node->Right)
        {
            Node->Min = FVector(
                (std::min)(Node->Left->Min.X, Node->Right->Min.X),
                (std::min)(Node->Left->Min.Y, Node->Right->Min.Y),
                (std::min)(Node->Left->Min.Z, Node->Right->Min.Z));

            Node->Max = FVector(
                (std::max)(Node->Left->Max.X, Node->Right->Max.X),
                (std::max)(Node->Left->Max.Y, Node->Right->Max.Y),
                (std::max)(Node->Left->Max.Z, Node->Right->Max.Z));
        }

        Node = Node->Parent;
    }
}

void FSceneBVHNode::Insert(
    FSceneBVHNode*& Root,
    FSceneBVHNode* Leaf)
{
    if (!Root)
    {
        Root = Leaf;
        Leaf->Parent = nullptr;
        return;
    }

    // 합친 AABB의 면적이 작은 자식을 따라 내려간다.
    FSceneBVHNode* Sibling = Root;
    while (Sibling->Component == nullptr)
    {
        const float LeftArea = JoinedArea(Sibling->Left, Leaf);
        const float RightArea = JoinedArea(Sibling->Right, Leaf);
        Sibling = (LeftArea <= RightArea)
            ? Sibling->Left
            : Sibling->Right;
    }

    FSceneBVHNode* OldParent = Sibling->Parent;
    FSceneBVHNode* NewParent = new FSceneBVHNode;

    NewParent->Parent = OldParent;
    NewParent->Left = Sibling;
    NewParent->Right = Leaf;

    Sibling->Parent = NewParent;
    Leaf->Parent = NewParent;

    if (OldParent)
    {
        if (OldParent->Left == Sibling)
            OldParent->Left = NewParent;
        else
            OldParent->Right = NewParent;
    }
    else
    {
        Root = NewParent;
    }

    RefitParents(NewParent);
}

void FSceneBVHNode::Detach(
    FSceneBVHNode*& Root,
    FSceneBVHNode* Leaf)
{
    if (Leaf == Root)
    {
        Root = nullptr;
        Leaf->Parent = nullptr;
        return;
    }

    FSceneBVHNode* Parent = Leaf->Parent;
    FSceneBVHNode* Sibling =
        (Parent->Left == Leaf) ? Parent->Right : Parent->Left;
    FSceneBVHNode* Grandparent = Parent->Parent;

    if (Grandparent)
    {
        if (Grandparent->Left == Parent)
            Grandparent->Left = Sibling;
        else
            Grandparent->Right = Sibling;

        Sibling->Parent = Grandparent;
    }
    else
    {
        Root = Sibling;
        Sibling->Parent = nullptr;
    }

    Leaf->Parent = nullptr;
    delete Parent;

    RefitParents(Grandparent);
}

FSceneBVHNode* FSceneBVHNode::Update(
    FSceneBVHNode*& Root,
    FSceneBVHNode* Leaf,
    UStaticMeshComponent* Component)
{
    if (!Component)
        return Leaf;

    FVector WorldMin;
    FVector WorldMax;

    if (!CalculateWorldBounds(Component, WorldMin, WorldMax))
    {
        Remove(Root, Leaf);
        return nullptr;
    }

    // 드래그 중 실제 AABB가 Fat AABB 안에 있으면 트리는 그대로 둔다.
    if (Leaf && Contains(Leaf, WorldMin, WorldMax))
        return Leaf;

    if (Leaf)
    {
        Detach(Root, Leaf);
    }
    else
    {
        Leaf = new FSceneBVHNode;
        Leaf->Component = Component;
    }

    const FVector Margin(0.25f, 0.25f, 0.25f);
    Leaf->Min = WorldMin - Margin;
    Leaf->Max = WorldMax + Margin;

    Insert(Root, Leaf);
    return Leaf;
}

void FSceneBVHNode::Remove(
    FSceneBVHNode*& Root,
    FSceneBVHNode* Leaf)
{
    if (!Leaf)
        return;

    Detach(Root, Leaf);
    delete Leaf;
}

void FSceneBVHNode::Clear(FSceneBVHNode*& Root)
{
    if (!Root)
        return;

    Clear(Root->Left);
    Clear(Root->Right);

    delete Root;
    Root = nullptr;
}

