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

private:
	FEditor* Editor;

	uint32 TotalPickCount = 0;
	double LastPickTimeMs = 0.0;
	double TotalPickTimeMs = 0.0;

	void PickPrimitives(UScene* Scene, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);
	void PickIcon(UScene* Scene, const UCameraComponent* Camera, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);

	// Todo: BVH
	void TestPrimitive(UPrimitiveComponent* Primitive, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);
	//void PickBVHNodeRecursive(const FSceneBVHNode* Node, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject);

	// Todo: BVH Mesh
	//bool PickMeshBVHNodeRecursive(const FMeshBVHNode* Node, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, float& ClosestDistance);

	void PickBVHNodeRecursive(const FSceneBVHNode* Node, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject, float EnterDistance = -1.0f);
	bool PickMeshBVHNodeRecursive(const FMeshBVHNode* Node, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, float& ClosestDistance, float EnterDistance = -1.0f);
};



