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
	TArray<FPrimitiveRenderData>& RenderList, TArray<FRenderObjectData>& Objects, const FFrustum* Frustum)
{
	RenderList.Empty();
	if (!Editor || !Scene || !Camera) return;

	const UActorComponent* SelectedComponent = Editor->GetSelectedSceneComponent();

	Scene->ForEachPrimitive(
		[&](UPrimitiveComponent* Primitive)
		{
			if (!Primitive->IsVisible())
			{
				return;
			}
			FRenderObjectData Object{};
			Object.World = Primitive->GetRenderWorldMatrix(Camera);
			Object.SortCenterWS = Object.World.GetOrigin();
			FVector LocalMin{};
			FVector LocalMax{};
			if (Primitive->GetLocalBounds(LocalMin, LocalMax))
			{
				Object.WorldBounds = FBoundingBox(LocalMin, LocalMax).TransformBounds(Object.World);
				Object.bHasWorldBounds = true;
				Object.SortCenterWS = Object.World.TransformPosition((LocalMin + LocalMax) * 0.5f);

				// 화면 밖 객체는 요청 생성 전에 제외하고, Bounds가 없으면 그대로 수집한다.
				if (Frustum && !Frustum->Intersects(Object.WorldBounds)) return;
			}

			const int32 FirstIndex = RenderList.Num();
			Primitive->CreateRenderData(RenderList, Primitive == SelectedComponent);
			const int32 EndIndex = RenderList.Num();
			if (FirstIndex == EndIndex) { return; }
			// 기존 Objects 뒤에 추가하고 이번 컴포넌트의 모든 섹션에 같은 인덱스를 부여한다.
			const uint32 ObjectIndex = static_cast<uint32>(Objects.Num());
			Objects.Add(Object);
			for (int32 Index = FirstIndex; Index < EndIndex; ++Index)
			{
				RenderList[Index].ObjectIndex = ObjectIndex;
			}
		});

	//SpotLight를 렌더링하기위한 임시 순회, 차후에 분리 해야함.
	Scene->ForEachActor([&](AActor* Actor)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component->IsA(USpotLightComponent::GetClass()))
				{
					continue;
				}

				const auto* SpotLight = static_cast<const USpotLightComponent*>(Component);

				if (!SpotLight->IsVisible())
				{
					continue;
				}

				FPrimitiveRenderData Data = SpotLight->BuildIconRenderData(Camera, SelectedComponent==SpotLight);

				if (!Data.Material || Data.Geometry.MeshPageId == InvalidRenderId || Data.Geometry.IndexCount == 0)
				{
					continue;
				}

				FRenderObjectData Object{};
				Object.World = SpotLight->GetIconWorldMatrix(Camera);
				Object.SortCenterWS = Object.World.GetOrigin();

				Data.ObjectIndex = static_cast<uint32>(Objects.Num());

				Objects.Add(Object);
				RenderList.Add(Data);
			}
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

TArray<FWorldTextItem> RenderUtil::GetTextRenderList(UScene* Scene, const UCameraComponent* Camera, bool bShowUUIDWidgets)
{
	TArray<FWorldTextItem> TextList;
	if (!Scene || !Camera) return TextList;

	if (bShowUUIDWidgets)
	{
		Scene->ForEachWidget(
			[&TextList, Camera](UWidgetComponent* Widget)
			{
				if (!Widget->IsVisible())
				{
					return;
				}

				FWorldTextItem Item;
				if (Widget->BuildTextItem(Camera, Item))
				{
					TextList.Add(Item);
				}
			}
		);
	}

	Scene->ForEachPrimitive(
		[&TextList, Camera](UPrimitiveComponent* Primitive)
		{
			if (Primitive->IsA(UTextComponent::GetClass()))
			{
				FWorldTextItem Item;
				auto* Text = static_cast<UTextComponent*>(Primitive);

				if (!Text->IsVisible())
				{
					return;
				}

				if (Text->BuildTextItem(Camera, Item))
				{
					TextList.Add(Item);
				}
			}
		}
	);
	return TextList;
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
