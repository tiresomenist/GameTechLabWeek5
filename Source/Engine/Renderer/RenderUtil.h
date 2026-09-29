#pragma once

#include "Core/Container/Array.h"
#include "Engine/Renderer/Text/WorldTextItem.h"
#include "Engine/Renderer/Line/LineDrawRequest.h"
#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Renderer/Frustum.h"
#include <d3d11.h>
#include "Engine/Renderer/ViewRenderData.h"
class UScene;
class FEditor;
class UCameraComponent;
struct FPrimitiveRenderData;
enum class EViewportType;

namespace RenderUtil
{
	void GetRenderList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
		TArray<FPrimitiveRenderData>& RenderList, TArray<FRenderObjectData>& Objects, const FFrustum* Frustum = nullptr);
	TArray<FPrimitiveRenderData> GetGizmoList(FEditor* Editor, UScene* Scene,
			const UCameraComponent* Camera,	const D3D11_VIEWPORT& Viewport,
		TArray<FRenderObjectData>& Objects);

	void GetTextRenderList(UScene* Scene, const UCameraComponent* Camera, bool bShowUUIDWidgets, FViewRenderData& OutData);	
	void SubmitLineDrawRequests(FEditor* Editor,UScene* Scene, const UCameraComponent* Camera,
		const FViewSettings& ViewSettings, const FLineRequestConsumer& Submit, EViewportType InViewtype);
};
