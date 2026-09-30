#pragma once

#include "Core/Math/Vector.h"

class UScene;
class FEditor;
class UPrimitiveComponent;
class USceneComponent;
class UCameraComponent;

// Todo: BVH Mesh
struct FMeshBVHNode;
struct FMeshResource;
class FMeshBVH;

struct FRay {
	FVector Origin;
	FVector Direction;
};

struct FRayAABBCache;


// Todo: BVH
class FSceneBVHNode;

class FObjectPicker
{
public:
	FObjectPicker(FEditor* InEditor);
	bool MakeWorldRay(FRay& OutRay, D3D11_VIEWPORT InViewport);
	bool RayTriangleIntersect(const FRay& Ray, FVector A, FVector B, FVector C, float& OutDistance);
	bool RayAABBIntersect(const FRay& Ray, const FVector& BoundsMin, const FVector& BoundsMax, float MaxDistance, float& OutDistance);
	USceneComponent* Pick();
	// 기존 로그와 같은 누적 피킹 값을 읽기 전용으로 제공합니다.
	uint32 GetTotalPickCount() const { return TotalPickCount; }
	double GetLastPickTimeMs() const { return LastPickTimeMs; }
	double GetTotalPickTimeMs() const { return TotalPickTimeMs; }
	double GetAveragePickTimeMs() const { return TotalPickCount ? TotalPickTimeMs / TotalPickCount : 0.0; }

private:
	FEditor* Editor;

	uint32 TotalPickCount = 0;
	double LastPickTimeMs = 0.0;
	double TotalPickTimeMs = 0.0;

	void PickPrimitives(UScene* Scene, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);
	void PickIcon(UScene* Scene, const UCameraComponent* Camera, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);

	// Todo: BVH
	void TestPrimitive(UPrimitiveComponent* Primitive, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);
	bool RayAABBIntersectCached(const FRay& Ray, const FRayAABBCache& Cache, const FVector& BoundsMin, const FVector& BoundsMax, float MaxDistance, float& OutDistance);
	//void PickBVHNodeRecursive(const FSceneBVHNode* Node, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);

	// Todo: BVH Mesh
	//bool PickMeshBVHNodeRecursive(const FMeshBVHNode* Node, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, float& ClosestDistance);

	void PickBVHNodeRecursive(const FSceneBVHNode* Node, const FRay& Ray, const FRayAABBCache& Cache, float& ClosestDistance, USceneComponent*& SelectedObject, float EnterDistance = -1.0f);
	bool PickMeshBVHNodeRecursive(const FMeshBVHNode* Node, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, const FRayAABBCache& Cache, float& ClosestDistance, float EnterDistance = -1.0f);
};



