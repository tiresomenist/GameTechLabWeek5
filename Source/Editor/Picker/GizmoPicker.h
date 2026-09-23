#pragma once

#include "Engine/Component/CameraComponent.h"
#include "Engine/Resource/ResourceManager.h"
#include "Editor/Gizmo/Gizmo.h"

class UScene;
class FEditor;

struct FRay;

class FGizmoPicker
{
public:
	FGizmoPicker(FEditor* InEditor);
	~FGizmoPicker();
	bool RayTriangleIntersect(const FRay& Ray, FVector A, FVector B, FVector C, float& OutDistance);
	bool MakeWorldRay(FRay& OutRay, D3D11_VIEWPORT InViewport);
	int Pick(UGizmo* InGizmos, D3D11_VIEWPORT InViewport);
private:
	FEditor* Editor;
};
