#include "pch.h"
#include "Gizmo.h"
#include "Engine/Object/Object.h"
#include "Engine/Object/ClassType.h"
#include "Engine/Renderer/VertexSimple.h"

void UGizmo::Initialize(FEditor* InEditor)
{
	Editor = InEditor;
}

TArray<FPrimitiveRenderData> UGizmo::GetRenderData(const UCameraComponent* Camera, const D3D11_VIEWPORT& Viewport)
{
	return TArray<FPrimitiveRenderData>();
}