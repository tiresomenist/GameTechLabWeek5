
#include "pch.h" 
// Todo: BVH Mesh
#include "MeshBVH.h"

FMeshBVH::~FMeshBVH()
{
    Clear();
}

void FMeshBVH::Clear()
{
    // 이 BVH가 생성한 노드를 각각 한 번씩 해제한다.
    {
        for (FMeshBVHNode* Node : AllocatedNodes)
        {
            delete Node;
        }
    }

    AllocatedNodes.Empty();
    PickTriangles.Empty();
    RootOrNull = nullptr;
}


void FMeshBVH::Build(const TArray<FVector>& Positions, const TArray<uint32>& Indices)
{
    Clear();

    TArray<FMeshTriangleInfo> TriangleInfos;

    // 인덱스가 유효한 삼각형 목록인지 검사한다.
    // Todo: Magic number
    if (Indices.IsEmpty() || Indices.Num() % 3 != 0)
    {
        return;
    }

    for (uint32 VertexIndex : Indices)
    {
        if (VertexIndex >= static_cast<uint32>(Positions.Num()))
        {
            return;
        }
    }

    // 각 삼각형의 로컬 AABB와 중심점을 계산한다.
    const uint32 TrianglesCount = static_cast<uint32>(Indices.Num() / 3);
    TriangleInfos.Reserve(TrianglesCount);

    for (uint32 TriangleIndex = 0; TriangleIndex < TrianglesCount; ++TriangleIndex)
    {
        const uint32 Base = TriangleIndex * 3;
        const FVector& A = Positions[Indices[Base]];
        const FVector& B = Positions[Indices[Base + 1]];
        const FVector& C = Positions[Indices[Base + 2]];

        FMeshTriangleInfo TriangleInfo;
        TriangleInfo.TriangleIndex = TriangleIndex;
        TriangleInfo.BoundsMin = FVector(std::min({ A.X, B.X, C.X }), std::min({ A.Y, B.Y, C.Y }), std::min({ A.Z, B.Z, C.Z }));
        TriangleInfo.BoundsMax = FVector(std::max({ A.X, B.X, C.X }), std::max({ A.Y, B.Y, C.Y }), std::max({ A.Z, B.Z, C.Z }));
        TriangleInfo.Centroid = (A + B + C) / 3.0f;

        TriangleInfos.Add(TriangleInfo);
    }

    RootOrNull = BuildNodesIterative(TriangleInfos, 0, static_cast<uint32>(TriangleInfos.Num()));

    // 리프 순서로 정점과 두 변을 한 번만 계산해 보관한다.
    PickTriangles.Reserve(TriangleInfos.Num());
    for (uint32 i = 0; i < static_cast<uint32>(TriangleInfos.Num()); ++i)
    {
        const uint32 Base = TriangleInfos[i].TriangleIndex * 3;
        const FVector& A = Positions[Indices[Base]];
        const FVector& B = Positions[Indices[Base + 1]];
        const FVector& C = Positions[Indices[Base + 2]];
        PickTriangles.Add({ A, B - A, C - A });
    }
}

FMeshBVHNode* FMeshBVH::BuildNodesIterative(TArray<FMeshTriangleInfo>& TriangleInfos, uint32 StartIndex, uint32 Count)
{
    assert(Count > 0);
    assert(StartIndex + Count <= static_cast<uint32>(TriangleInfos.Num()));

    struct FBuildTask
    {
        FMeshBVHNode* Node;
        uint32 StartIndex;
        uint32 Count;
    };

    FMeshBVHNode* Root = new FMeshBVHNode;
    AllocatedNodes.Add(Root);

    TArray<FBuildTask> Stack;
    Stack.Add({ Root, StartIndex, Count });

    while (!Stack.IsEmpty())
    {
        const FBuildTask Task = Stack.Pop();
        FMeshBVHNode* Node = Task.Node;
        StartIndex = Task.StartIndex;
        Count = Task.Count;

        // 그룹에 속한 삼각형들의 AABB를 합쳐 노드 AABB를 만든다.
        Node->LocalMin = TriangleInfos[StartIndex].BoundsMin;
        Node->LocalMax = TriangleInfos[StartIndex].BoundsMax;

        for (uint32 i = StartIndex + 1; i < StartIndex + Count; ++i)
        {
            const FMeshTriangleInfo& TriangleInfo = TriangleInfos[i];

            Node->LocalMin.X = std::min(Node->LocalMin.X, TriangleInfo.BoundsMin.X);
            Node->LocalMin.Y = std::min(Node->LocalMin.Y, TriangleInfo.BoundsMin.Y);
            Node->LocalMin.Z = std::min(Node->LocalMin.Z, TriangleInfo.BoundsMin.Z);

            Node->LocalMax.X = std::max(Node->LocalMax.X, TriangleInfo.BoundsMax.X);
            Node->LocalMax.Y = std::max(Node->LocalMax.Y, TriangleInfo.BoundsMax.Y);
            Node->LocalMax.Z = std::max(Node->LocalMax.Z, TriangleInfo.BoundsMax.Z);
        }

        // 삼각형이 충분히 적으면 이 노드를 리프로 만든다.
        if (Count <= MAX_TRIANGLES_PER_LEAF)
        {
            Node->FirstTriangleOffset = StartIndex;
            Node->TrianglesCount = Count;

            continue;
        }

        FVector CentroidMin = TriangleInfos[StartIndex].Centroid;
        FVector CentroidMax = TriangleInfos[StartIndex].Centroid;

        // 중심점들이 각 축에서 차지하는 범위를 구한다.
        for (uint32 i = StartIndex + 1; i < StartIndex + Count; ++i)
        {
            const FVector& Center = TriangleInfos[i].Centroid;

            CentroidMin.X = (std::min)(CentroidMin.X, Center.X);
            CentroidMin.Y = (std::min)(CentroidMin.Y, Center.Y);
            CentroidMin.Z = (std::min)(CentroidMin.Z, Center.Z);

            CentroidMax.X = (std::max)(CentroidMax.X, Center.X);
            CentroidMax.Y = (std::max)(CentroidMax.Y, Center.Y);
            CentroidMax.Z = (std::max)(CentroidMax.Z, Center.Z);
        }

        const FVector Extent = CentroidMax - CentroidMin;

        // 모든 중심점이 같다면 공간적으로 나눌 수 없으므로 리프로 만든다.
        if (Extent.X == 0.0f && Extent.Y == 0.0f && Extent.Z == 0.0f)
        {
            Node->FirstTriangleOffset = StartIndex;
            Node->TrianglesCount = Count;
            continue;
        }

        int Axis = 0;

        // 중심점이 가장 넓게 퍼진 축을 고른다.
        if (Extent.Y > Extent[Axis])
        {
            Axis = 1;
        }

        if (Extent.Z > Extent[Axis])
        {
            Axis = 2;
        }

        // 선택한 축의 중심점 순서로 현재 범위만 정렬한다.
        std::sort(TriangleInfos.begin() + StartIndex, TriangleInfos.begin() + StartIndex + Count,
            [Axis](const FMeshTriangleInfo& A, const FMeshTriangleInfo& B) {
                if (A.Centroid[Axis] == B.Centroid[Axis])
                {
                    return A.TriangleIndex < B.TriangleIndex;
                }

                return A.Centroid[Axis] < B.Centroid[Axis];
            }
        );

        const uint32 MidIndex = StartIndex + Count / 2;
        Node->LeftChild = new FMeshBVHNode;
        Node->RightChild = new FMeshBVHNode;
        AllocatedNodes.Add(Node->LeftChild);
        AllocatedNodes.Add(Node->RightChild);

        Stack.Add({ Node->RightChild, MidIndex, StartIndex + Count - MidIndex });
        Stack.Add({ Node->LeftChild, StartIndex, MidIndex - StartIndex });
    }

    return Root;
}
