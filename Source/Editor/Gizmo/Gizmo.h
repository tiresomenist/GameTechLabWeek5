#pragma once
#include "Core/Container/Array.h"
#include "Editor/Editor.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Object/Object.h"
#include "Engine/Resource/ResourceManager.h"
#include "Editor/Gizmo/GizmoMode.h"
#include "Engine/Renderer/Grid.h"
#include "Engine/Renderer/Line/LineDrawRequest.h"
#include <d3d11.h>

class UCameraComponent;

struct FGizmoHandle
{
	int32 Axis = -1;       // 어느 축이 선택됐는가?
	FMeshResource* Mesh = nullptr;   // 무엇을 그리는가?
	FMatrix WorldMatrix = FMatrix::Identity;  // 어디에 어떻게 놓는가?
	int32 Topology = -1; //어떤식으로 그려지는가? 0:LINELIST 1:TRIANGLELIST
	bool IsSeleted = false;
};

class UGizmo : public UObject
{

    UCLASS(UGizmo, "Gizmo", UObject)

protected:
	EGizmoMode Mode = EGizmoMode::Translate;
	FEditor* Editor = nullptr;
	TArray<FGizmoHandle> Handles;
public:

	void Initialize(FEditor* InEditor);
	const TArray<FGizmoHandle>& GetHandles() const { return Handles; }

	// 렌더러에게 전달할 렌더 정보
	virtual TArray<FPrimitiveRenderData> GetRenderData(const UCameraComponent* Camera,
		const D3D11_VIEWPORT& Viewport);
	TArray<FMeshResource*> GetMeshResources() const {
	TArray< FMeshResource*> GizmoArray;
	for (auto& handle : GetHandles()) {
		GizmoArray.Add(handle.Mesh);
	}
		return GizmoArray;
	}

	virtual FLineDrawRequest BuildLineDrawRequest(const FGrid& Grid,const FVector& CameraPosition) const
	{
		return {};
	}

};
