#include "pch.h"
#include "ObjectAxisGizmo.h"
#include "Engine/Component/SceneComponent.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/MeshNames.h"
#include "Engine/Engine.h"
#include "Engine/Component/CameraComponent.h"
#include <cmath>

//UObjectAxisGizmo::UObjectAxisGizmo()

void UObjectAxisGizmo::Initialize()
{
	Handles = {
	{ 0, nullptr, FMatrix::MakeRotationYMatrix(PI / 2) },
	{ 1, nullptr, FMatrix::MakeRotationXMatrix(-PI / 2) },
	{ 2, nullptr, FMatrix::Identity }
	};
	Mode = EGizmoMode::Translate;
	SetMode(Mode);
}

bool UObjectAxisGizmo::UpdateTransform()
{
	if (!Editor)
	{
		return false;
	}

	return UpdateTransform(Editor->GetEditorCamera(), GEngine::GetInstance()->GetViewport());
}

bool UObjectAxisGizmo::UpdateTransform(const UCameraComponent* Camera, const D3D11_VIEWPORT& Viewport)
{
	if (!Editor || !Camera || Handles.Num() != 3)
	{
		return false;
	}

	auto* SelectedObject = Editor->GetTransformTarget();

	if (!SelectedObject)
	{
		return false;
	}

	if (!std::isfinite(Viewport.Width) ||!std::isfinite(Viewport.Height) ||	Viewport.Width <= 0.0f ||Viewport.Height <= 0.0f)
	{
		return false;
	}

	const FVector WorldLocation =SelectedObject->GetWorldMatrix().GetOrigin();

	const FVector4 ViewPosition = FVector4(WorldLocation, 1.0f) * Camera->GetViewMatrix();

	const float ViewDepth = ViewPosition.X;

	if (!std::isfinite(ViewDepth) ||ViewDepth <= Camera->GetNearZ() ||ViewDepth >= Camera->GetFarZ())
	{
		return false;
	}

	float VisibleWorldHeight;

	if (Camera->GetIsPerspective())
	{
		const float FOV = Camera->GetFOV();

		if (!std::isfinite(FOV) || FOV <= 0.0f || FOV >= PI) return false;

		VisibleWorldHeight = 2.0f * ViewDepth * std::tan(FOV * 0.5f);
	}
	else
	{
		VisibleWorldHeight = Camera->GetOrthoHeight();
	}

	if (!std::isfinite(VisibleWorldHeight) ||VisibleWorldHeight <= 0.0f)
	{
		return false;
	}

	const float TargetPixels = Viewport.Height * GizmoScreenHeightRatio;

	const float WorldUnitsPerPixel = VisibleWorldHeight / Viewport.Height;

	if (HandleBaseLengths.Num() != 3) return false;
	TArray<FMatrix> ScaleMatrices;
	for (const float BaseLength : HandleBaseLengths)
	{
		if (!std::isfinite(BaseLength) || BaseLength <= 0.0f) return false;
		const float DisplayScale = WorldUnitsPerPixel * TargetPixels / BaseLength;
		if (!std::isfinite(DisplayScale) || DisplayScale <= 0.0f) return false;
		ScaleMatrices.Add(FMatrix::MakeScaleMatrix(GizmoScale * DisplayScale));
	}
	
	const FMatrix TranslationMatrix = FMatrix::MakeTranslationMatrix(WorldLocation);

	FMatrix RotationMatirx = FMatrix::Identity;
	if (Mode == EGizmoMode::Scale) {
		RotationMatirx = SelectedObject->GetRelativeRotation().ToRotationMatrix();
	}

	Handles[0].WorldMatrix = ScaleMatrices[0] * FMatrix::MakeRotationYMatrix(PI / 2) * RotationMatirx * TranslationMatrix;
	Handles[1].WorldMatrix = ScaleMatrices[1] * FMatrix::MakeRotationXMatrix(-PI / 2) * RotationMatirx * TranslationMatrix;
	Handles[2].WorldMatrix = ScaleMatrices[2] * RotationMatirx * TranslationMatrix;

	return true;
}

TArray<FPrimitiveRenderData> UObjectAxisGizmo::GetRenderData(const UCameraComponent* Camera,
	const D3D11_VIEWPORT& Viewport, TArray<FRenderObjectData>& Objects)
{
	if (!UpdateTransform(Camera, Viewport))
	{
		return {};
	}
	if (ColorMaterial.MaterialId == InvalidRenderId)
	{
		ColorMaterial = GResourceManager::GetInstance()->CreateColorMaterial();
	}

	switch (Mode)
	{
	case EGizmoMode::Translate:
		return GetTranslateRenderData(Objects);

	case EGizmoMode::Rotate:
		return GetRotateRenderData(Objects);

	case EGizmoMode::Scale:
		return GetScaleRenderData(Objects);
	}

	return {};
}

void UObjectAxisGizmo::AppendHandleRenderData(const FGizmoHandle& Handle, D3D11_PRIMITIVE_TOPOLOGY Topology,
	TArray<FRenderObjectData>& Objects, TArray<FPrimitiveRenderData>& Result)
{
	const FMeshResource* Mesh = Handle.Mesh;
	if (!Mesh) { return; }

	const FMeshAllocation& Allocation = Mesh->GetAllocation();
	if (Allocation.MeshPageId == InvalidRenderId ||	Allocation.IndexCount == 0){ return; }

	// 이 뷰에서 사용할 월드 행렬을 값으로 복사한다.
	FRenderObjectData Object;
	Object.World = Handle.WorldMatrix;
	Object.SortCenterWS = Object.World.GetOrigin();

	if (Mesh->HasBounds())
	{
		const FVector LocalCenter =	(Mesh->GetBoundsMin() + Mesh->GetBoundsMax()) * 0.5f;
		Object.SortCenterWS = Object.World.TransformPosition(LocalCenter);
	}

	const uint32 ObjectIndex = static_cast<uint32>(Objects.Num());

	Objects.Add(Object);

	FPrimitiveRenderData Data;

	// 기즈모 핸들은 해당 메시 전체를 그린다.
	Data.Geometry.MeshPageId = Allocation.MeshPageId;
	Data.Geometry.FirstIndex = Allocation.FirstIndex;
	Data.Geometry.IndexCount = Allocation.IndexCount;
	Data.Geometry.BaseVertex = Allocation.BaseVertex;

	Data.Material = &ColorMaterial;
	Data.ObjectIndex = ObjectIndex;
	Data.Topology = Topology;

	// 기즈모 선택 표시용. 일반 메시의 외곽선 플래그는 넣지 않는다.
	Data.Flags =(Editor->GetActiveGizmoAxis() == Handle.Axis)? Primitive_Selected : Primitive_None;

	Result.Add(Data);
}

TArray<FPrimitiveRenderData> UObjectAxisGizmo::GetTranslateRenderData(TArray<FRenderObjectData>& Objects)
{
	TArray<FPrimitiveRenderData> Result;

	AppendHandleRenderData(Handles[0], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Objects, Result);

	AppendHandleRenderData(Handles[1], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Objects, Result);

	AppendHandleRenderData(Handles[2], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,Objects,Result);

	return Result;
}

TArray<FPrimitiveRenderData> UObjectAxisGizmo::GetRotateRenderData(TArray<FRenderObjectData>& Objects)
{
	TArray<FPrimitiveRenderData> Result;

	AppendHandleRenderData(Handles[0],D3D11_PRIMITIVE_TOPOLOGY_LINELIST,Objects,Result);

	AppendHandleRenderData(Handles[1],D3D11_PRIMITIVE_TOPOLOGY_LINELIST,Objects,Result);

	AppendHandleRenderData(Handles[2],D3D11_PRIMITIVE_TOPOLOGY_LINELIST,Objects,Result);

	return Result;
}

TArray<FPrimitiveRenderData> UObjectAxisGizmo::GetScaleRenderData(TArray<FRenderObjectData>& Objects)
{
	TArray<FPrimitiveRenderData> Result;

	AppendHandleRenderData(Handles[0], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Objects, Result);

	AppendHandleRenderData(Handles[1], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Objects, Result);

	AppendHandleRenderData(Handles[2], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Objects, Result);

	return Result;
}

FMatrix UObjectAxisGizmo::GetXAxisWorldMatirx() const
{
	return Handles[0].WorldMatrix;
}

FMatrix UObjectAxisGizmo::GetYAxisWorldMatirx() const
{
	return Handles[1].WorldMatrix;
}

FMatrix UObjectAxisGizmo::GetZAxisWorldMatirx() const
{
	return Handles[2].WorldMatrix;
}

void UObjectAxisGizmo::SetMode(EGizmoMode InMode)
{
	Mode = InMode;

	const FMeshNames& MeshNames = GetMeshNames();
	TArray<FName> Names;

	switch (Mode)
	{
	case EGizmoMode::Translate:
		Names = {MeshNames.ArrowRed,MeshNames.ArrowGreen,MeshNames.ArrowBlue};
		break;

	case EGizmoMode::Rotate:
		Names = {MeshNames.RotateRed,MeshNames.RotateGreen,MeshNames.RotateBlue};
		break;

	case EGizmoMode::Scale:
		Names = {MeshNames.ScaleRed,MeshNames.ScaleGreen,MeshNames.ScaleBlue};
		break;
	}

	HandleBaseLengths = { 0.0f, 0.0f, 0.0f };
	for (int32 Axis = 0; Axis < 3; ++Axis) {

		Handles[Axis].Mesh = GResourceManager::GetInstance()->GetPrimitive(Names[Axis]);
		Handles[Axis].Topology = Mode == EGizmoMode::Rotate ? 0 : 1;

		const auto* Mesh = Handles[Axis].Mesh;
		if (!Mesh) continue;
		// 이동/스케일은 원점부터 Z축 끝까지, 회전은 XY 평면의 반지름.
		// 메시 정점 순회는 모드 변경 때만 수행한다.
		for (const FVector& Position : Mesh->GetPositions())
		{
			const float Length = Mode == EGizmoMode::Rotate
				? std::hypot(Position.X * GizmoScale.X, Position.Y * GizmoScale.Y)
				: std::fabs(Position.Z * GizmoScale.Z);
			if (!std::isfinite(Length))
			{
				HandleBaseLengths[Axis] = 0.0f;
				break;
			}
			if (Length > HandleBaseLengths[Axis]) HandleBaseLengths[Axis] = Length;
		}
	}
		
		
}
