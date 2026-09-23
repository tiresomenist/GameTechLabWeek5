#pragma once

#include "Core/Container/Array.h"
#include "Engine/Renderer/Text/WorldTextItem.h"
#include "Engine/Renderer/Line/LineBatcher.h"
#include "Engine/Renderer/Line/LineDrawRequest.h"
#include "Engine/Renderer/ViewSettings.h"
#include <d3d11.h>

class UScene;
class FEditor;
class UCameraComponent;
struct FPrimitiveRenderData;
enum class EViewportType;

namespace RenderUtil
{
	TArray<FPrimitiveRenderData> GetRenderList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera);
	TArray<FPrimitiveRenderData> GetGizmoList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
		const D3D11_VIEWPORT& Viewport);
	TArray<FWorldTextItem> GetTextRenderList(UScene* Scene, const UCameraComponent* Camera,
		bool bShowUUIDWidgets);
	void SubmitLineDrawRequests(FEditor* Editor,UScene* Scene, const UCameraComponent* Camera,
		const FViewSettings& ViewSettings, FLineBatcher& Batcher, EViewportType InViewtype);
};
