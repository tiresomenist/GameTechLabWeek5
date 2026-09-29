#include "pch.h"
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
	TArray<FVisibleGridCell>& VisibleGridCells, const FFrustum* Frustum)
{
	RenderList.Empty();
	VisibleGridCells.Empty();
	if (!Editor || !Scene || !Camera) return;

	const UActorComponent* SelectedComponent = Editor->GetSelectedSceneComponent();

	auto AddPrimitive = [&](UPrimitiveComponent* Primitive, TArray<uint32>* GridPrimitiveIndices = nullptr)
		{
			if (!Primitive->IsVisible())
			{
				return;
			}
			
			/*const int32 FirstIndex = RenderList.Num();
			Primitive->CreateRenderData(RenderList, Camera, Primitive == SelectedComponent);
			const int32 EndIndex = RenderList.Num();
			if (FirstIndex == EndIndex)
			{
				return;
			}*/

			FRenderObjectData Object{};
			Object.SourcePrimitive = Primitive;
			Object.World = Primitive->GetRenderWorldMatrix(Camera);
			Object.SortCenterWS = Object.World.GetOrigin();
			FVector LocalMin{};
			FVector LocalMax{};
			if (Primitive->GetLocalBounds(LocalMin, LocalMax))
			{
				Object.WorldBounds = FBoundingBox(LocalMin, LocalMax).TransformBounds(Object.World);
				Object.bHasWorldBounds = true;
				Object.SortCenterWS = Object.World.TransformPosition((LocalMin + LocalMax) * 0.5f);

				if (Frustum && !Frustum->Intersects(Object.WorldBounds)) return;
			}

			const int32 FirstIndex = RenderList.Num();
			Primitive->CreateRenderData(RenderList, Camera, Primitive == SelectedComponent);
			const int32 EndIndex = RenderList.Num();
			if (FirstIndex == EndIndex) { return; }

			const uint32 ObjectIndex = static_cast<uint32>(Objects.Num());
			Objects.Add(Object);
			for (int32 Index = FirstIndex; Index < EndIndex; ++Index)
			{
				RenderList[Index].ObjectIndex = ObjectIndex;
				const FPrimitiveRenderData& Data = RenderList[Index];
				if (GridPrimitiveIndices && Data.Material &&
					Data.Material->BlendMode != EPrimitiveBlendMode::Additive &&
					(Data.Flags & Primitive_Selected) == 0)
				{
					GridPrimitiveIndices->Add(static_cast<uint32>(Index));
				}
			}
		};

	// 정적 메시를 고정 크기 셀에 한 번만 배치하고, 매 프레임에는 셀 AABB만 먼저 Frustum과 비교
	for (const FStaticUniformGridCell& Cell : Scene->GetStaticUniformGrid())
	{
		if (Frustum && !Frustum->Intersects(Cell.ContentBounds)) continue;
		FVisibleGridCell VisibleCell{};
		VisibleCell.Key = Cell.Key;
		VisibleCell.SpatialBounds = Cell.SpatialBounds;
		VisibleCell.OcclusionBounds = Cell.ContentBounds;
		for (UPrimitiveComponent* Primitive : Cell.Primitives)
		{
			AddPrimitive(Primitive, &VisibleCell.PrimitiveIndices);
		}
		if (!VisibleCell.PrimitiveIndices.IsEmpty()) VisibleGridCells.Add(std::move(VisibleCell));
	}

	for (UPrimitiveComponent* Primitive : Scene->GetStaticUniformGridFallbackPrimitives())
	{
		AddPrimitive(Primitive);
	}

	// 비정적 메시에는 기존 개별 경로를 유지
	Scene->ForEachPrimitive([&](UPrimitiveComponent* Primitive)
		{
			if (Primitive->IsA(UStaticMeshComponent::GetClass())) return;
			AddPrimitive(Primitive);
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
