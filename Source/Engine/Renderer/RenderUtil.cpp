#include "pch.h"
#include "Engine/Util/DebugCpuStats.h"
#include "RenderUtil.h"
#include "Core/Container/Array.h"
#include "Core/Math/Box.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Engine/Component/Primitive/TextComponent.h"
#include "Engine/Component/WidgetComponent.h"
#include "Engine/Component/CameraComponent.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Engine/Resource/MeshResource.h"
#include "Engine/Scene/Scene.h"
#include "Editor/Editor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Grid.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Component/Light/SpotLightComponent.h"
#include <string>
#include "Engine/Log.h"


namespace
{
	bool IsComponentSelected(const FEditor* Editor, const USceneComponent* Component)
	{
		if (!Editor || !Component) return false;
		return Editor->GetSelectedSceneComponent() == Component;
	}
}

// 현재 View의 Bounds와 Frustum을 검사하고 객체별 렌더 요청을 수집한다.
void RenderUtil::GetRenderList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
	TArray<FPrimitiveRenderData>& RenderList, TArray<FRenderObjectData>& Objects,
	const TArray<FGridCellCandidate>& RenderGridCells, const FFrustum* Frustum)
{
	FScopedDebugCpuTime CpuTime(EDebugCpuStat::Gather);
	RenderList.Empty();
	if (!Editor || !Scene || !Camera) return;

	const UActorComponent* SelectedComponent = Editor->GetSelectedSceneComponent();
	// 기존 LOD/임포스터의 카메라 좌표 기준을 유지하며 View당 한 번만 읽습니다.
	const FVector CameraLocation = Camera->GetRelativeLocation();

	auto AddPrimitive = [&](UPrimitiveComponent* Primitive, bool bSkipPrimitiveFrustum,
		const FBoundingBox* CachedWorldBounds = nullptr)
		{
			if (!Primitive->IsVisible())
			{
				return;
			}

			FRenderObjectData Object{};
			Object.SourcePrimitive = Primitive;
			Object.World = Primitive->GetRenderWorldMatrix(Camera);
			Object.SortCenterWS = Object.World.GetOrigin();
			bool bHasWorldCenter = CachedWorldBounds != nullptr;

			if (CachedWorldBounds)
			{
				Object.SortCenterWS = (CachedWorldBounds->Min + CachedWorldBounds->Max) * 0.5f;

				if (!bSkipPrimitiveFrustum)
				{
					if (Frustum && !Frustum->Intersects(*CachedWorldBounds)) return;
				}
			}
			else {
				FVector LocalMin{};
				FVector LocalMax{};
				const bool bHasLocalBounds = Primitive->GetLocalBounds(LocalMin, LocalMax);
				bHasWorldCenter = bHasLocalBounds;

				if (bHasLocalBounds)
				{
					if (bSkipPrimitiveFrustum)
					{
						// 셀 전체가 내부이면 객체 Bounds를 만들지 않고 LOD·정렬용 중심만 계산합니다.
						Object.SortCenterWS = Object.World.TransformPosition((LocalMin + LocalMax) * 0.5f);
					}
					else
					{
						// 경계 셀과 개별 객체는 Bounds 변환 중 계산한 중심까지 함께 재사용합니다.
						const FBoundingBox WorldBounds = FBoundingBox(LocalMin, LocalMax).
							TransformBounds(Object.World, &Object.SortCenterWS);

						if (Frustum && !Frustum->Intersects(WorldBounds)) return;
					}
				}
			}
			

			const int32 FirstIndex = RenderList.Num();

			// 캐시와 직접 계산 경로 모두 중심의 유효성을 LOD 수집에 전달합니다.
			const FPrimitiveRenderContext Context{Camera, Object.World, Object.SortCenterWS, CameraLocation, bHasWorldCenter};
			Primitive->CreateRenderData(RenderList, Context, Primitive == SelectedComponent);
			const int32 EndIndex = RenderList.Num();
			if (FirstIndex == EndIndex) { return; }

			const uint32 ObjectIndex = static_cast<uint32>(Objects.Num());
			Objects.Add(Object);

			for (int32 Index = FirstIndex; Index < EndIndex; ++Index)
			{
				RenderList[Index].ObjectIndex = ObjectIndex;

				if (RenderList[Index].bImpostor) {
					Objects[ObjectIndex].ImpostorCenterWS = RenderList[Index].ImpostorCenterWS;

					Objects[ObjectIndex].ImpostorSize = RenderList[Index].ImpostorSize;

					Objects[ObjectIndex].ImpostorUV = RenderList[Index].ImpostorUV;

					Objects[ObjectIndex].ImpostorCameraLocation = RenderList[Index].ImpostorCameraLocation;
				}
			}
		};

	for (const FGridCellCandidate& Candidate : RenderGridCells)
	{
		const FStaticUniformGridCell* Cell = Candidate.SourceCell;
		if (!Cell) { continue; }

		for (const FUniformGridPrimitive& GridPrimitive : Cell->Primitives)
		{
			AddPrimitive(GridPrimitive.Primitive, Candidate.bFullyInsideFrustum,
				&GridPrimitive.WorldBounds);
		}
	}

	for (UPrimitiveComponent* Primitive : Scene->GetStaticUniformGridFallback())
	{
		AddPrimitive(Primitive, false);
	}

	// 비정적 메시에는 기존 개별 경로를 유지
	Scene->ForEachNonStaticMesh([&](UPrimitiveComponent* Primitive)
		{
			AddPrimitive(Primitive, false);
		});

	//SpotLight를 렌더링하기위한 임시 순회, 차후에 Billborad로 확장할것임.
	Scene->ForEachBillboardIcon([&](USpotLightComponent* SpotLight)
		{
			if (!SpotLight->IsVisible()) return;

			// 선택 상태와 카메라 의존 렌더 데이터 생성은 기존 동작을 유지합니다.
			FPrimitiveRenderData Data = SpotLight->BuildIconRenderData(
				Camera, SelectedComponent == SpotLight);

			if (!Data.Material || Data.Geometry.MeshPageId == InvalidRenderId
				|| Data.Geometry.IndexCount == 0)
				return;

			FRenderObjectData Object{};
			Object.World = SpotLight->GetIconWorldMatrix(Camera);
			Object.SortCenterWS = Object.World.GetOrigin();

			// 기존 Objects 뒤에 추가하여 이전 ObjectIndex를 유지합니다.
			Data.ObjectIndex = static_cast<uint32>(Objects.Num());
			Objects.Add(Object);
			RenderList.Add(Data);
		});

	FDebugCpuStats::Get().AddRequests(static_cast<uint32>(RenderList.Num()));
	return;
}

TArray<FPrimitiveRenderData> RenderUtil::GetGizmoList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
	const D3D11_VIEWPORT& Viewport, TArray<FRenderObjectData>& Objects)
{
	TArray<FPrimitiveRenderData> RenderList;

	if (!Editor || !Scene || !Camera){ return RenderList; }

	for (UGizmo* Gizmo : Editor->GetGizmos())
	{
		TArray<FPrimitiveRenderData> Requests = Gizmo->GetRenderData(Camera, Viewport, Objects);

		for (const FPrimitiveRenderData& Data : Requests)
		{
			RenderList.Add(Data);
		}
	}
	return RenderList;
}

void RenderUtil::GetTextRenderList(UScene* Scene, const UCameraComponent* Camera, 
	bool bShowUUIDWidgets, FViewRenderData& OutData)
{
	OutData.TextCamera = Camera;
	OutData.TextRequests.Empty();
	if (!Scene || !Camera) return;

	if (bShowUUIDWidgets)
	{
		// 현재 수집된 메시만 연결한다. 프러스텀에서 탈락한 메시는 들어 있지 않다.
		TMap<const UPrimitiveComponent*, uint32> ObjectIndices;
		for (int32 Index = 0; Index < OutData.Objects.Num(); ++Index)
		{
			const UPrimitiveComponent* Source = OutData.Objects[Index].SourcePrimitive;
			if (Source) ObjectIndices.Add(Source, static_cast<uint32>(Index));
		}

		Scene->ForEachWidget([&](UWidgetComponent* Widget)
			{
				if (!Widget->IsVisible() || !Widget->GetOwner()) return;

				USceneComponent* Root = Widget->GetOwner()->GetRootComponent();
				if (!Root || !Root->IsA(UPrimitiveComponent::GetClass())) return;

				const auto* Primitive = static_cast<const UPrimitiveComponent*>(Root);
				if (!Primitive->IsVisible()) return;

				const uint32* ObjectIndex = ObjectIndices.Find(Primitive);

				// 일반 TextComponent는 메시 요청을 만들지 않으므로 예외로 남긴다.
				if (!ObjectIndex && !Primitive->IsA(UTextComponent::GetClass())) return;

				FTextDrawRequest Request{};
				Request.Widget = Widget;
				if (ObjectIndex) Request.OwnerObjectIndex = *ObjectIndex;
				OutData.TextRequests.Add(Request);
			});
	}

	Scene->ForEachText([&](UTextComponent* Text)
		{
			if (!Text->IsVisible()) return;

			FTextDrawRequest Request{};
			Request.TextComponent = Text;
			OutData.TextRequests.Add(Request);

		});

}

void RenderUtil::SubmitLineDrawRequests(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
	const FViewSettings& ViewSettings, const FLineRequestConsumer& Submit, EViewportType InViewtype)
{
	if (!Editor || !Scene|| !Camera) { return; }

	FLineDrawContext Context;
	Context.Camera = Camera;
	Context.bShowBounds = ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Bounds);
	Context.bShowPrimitives = ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Primitives);
	Context.ViewType = InViewtype;
	const USceneComponent* Selected = Editor->GetSelectedSceneComponent();

	if (Context.bShowBounds) 
	{
		// 각 프리미티브가 생성한 바운딩 박스 요청을 즉시 제출함
		Scene->ForEachPrimitive([&](UPrimitiveComponent* Primitive)
			{
				if (!Primitive->IsVisible())
				{
					return;
				}

				Context.bSelected = Selected == Primitive;
				Primitive->SubmitLineDrawRequests(Context, Submit);
			}
		);
	}
	else if (Selected && Selected->IsA(UPrimitiveComponent::GetClass()))
	{
		const auto* Primitive = static_cast<const UPrimitiveComponent*>(Selected);
		if (Primitive->IsVisible())
		{
			Context.bSelected = true;
			Primitive->SubmitLineDrawRequests(Context, Submit);
		}
	}

	// 추후 에디터-게임씬 분리시 에디터 단으로 이동해야함
	if (Context.bShowPrimitives)
	{
		const USceneComponent* Selected =
			Editor->GetSelectedSceneComponent();

		if (Selected && Selected->IsA(USpotLightComponent::GetClass()))
		{
			const auto* SpotLight =
				static_cast<const USpotLightComponent*>(Selected);

			if (SpotLight->IsVisible())
			{
				Submit(SpotLight->BuildConeLineDrawRequest());
			}
		}
	}

	const FVector CameraPosition = Camera->GetWorldLocation();
	const FGrid& GridSettings = Editor->GetGrid();

	// 생성된 그리드 요청을 즉시 제출함
	if (ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Grid))
	{
		for (UGrid* Grid : Editor->GetGrids())
		{
			Submit(Grid->BuildLineDrawRequest(GridSettings,CameraPosition, InViewtype));
		}
	}

	// 생성된 기즈모 라인 요청을 즉시 제출함
	if (ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::WorldAxis)) {
		for (UGizmo* Gizmo : Editor->GetGizmos())
		{
			Submit(Gizmo->BuildLineDrawRequest(GridSettings, CameraPosition));
		}
	}
}

void RenderUtil::GatherGridCellCandidates(UScene* Scene, const FFrustum* Frustum,
	TArray<FGridCellCandidate>& OutCandidates)
{
	OutCandidates.Empty();

	for (const FStaticUniformGridCell& Cell : Scene->GetStaticUniformGrid())
	{
		if (Cell.Primitives.IsEmpty()) { continue; }

		bool bFullyInsideFrustum = false;
		if (Frustum)
		{
			if(!Frustum->Intersects(Cell.ContentBounds)) { continue; }
			bFullyInsideFrustum = Frustum->Contains(Cell.ContentBounds);
		}

		FGridCellCandidate Candidate{};
		Candidate.Key = Cell.Key;
		Candidate.OcclusionBounds = Cell.ContentBounds;
		Candidate.SourceCell = &Cell;
		Candidate.bFullyInsideFrustum = bFullyInsideFrustum;

		OutCandidates.Add(Candidate);
	}
}
