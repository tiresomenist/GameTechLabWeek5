#pragma once

#include "Engine/Component/CameraComponent.h"
#include "ViewRenderer.h"
#include "ViewSettings.h"

enum class EViewportType
{
	Perspective,
	Top,
	Front,
	Right
};

class FViewportClient
{
public:
	void Initialize(EViewportType InType, UCameraComponent* InCamera);

	void SetRect(float x, float y, float Width, float Height);
	//내부적으로 비트마스킹으로 처리해줌.
	bool IsShowingUUIDLabels() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::UUID); }
	void SetShowUUIDLabels(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::UUID, bShow); }
	bool IsShowingBoundingBoxes() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Bounds); }
	void SetShowBoundingBoxes(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::Bounds, bShow); }
	EViewModeIndex GetViewMode() const { return ViewSettings.ViewMode; }
	void SetViewMode(EViewModeIndex InMode) { ViewSettings.ViewMode = InMode; }
	bool IsShowingPrimitives() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Primitives); }
	void SetShowPrimitives(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::Primitives, bShow); }
	bool IsShowingGrid() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Grid); }
	void SetShowGrid(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::Grid, bShow); }
	bool IsShowingWorldAxis() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::WorldAxis); }
	void SetShowWorldAxis(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::WorldAxis, bShow); }

	void SetViewSettings(const FViewSettings& InVS) { ViewSettings = InVS; }

	FRenderView GetRenderView() const;
	UCameraComponent* GetCamera() const;
	EViewportType GetViewportType() const { return ViewportType; }
	const FViewSettings& GetViewSettings() const { return ViewSettings; }
	const D3D11_VIEWPORT& GetViewportInfo() const { return ViewportInfo; }

	bool IsMouseInside(float ScreenX, float ScreenY) const;
	
private:
	UCameraComponent* Camera = nullptr;
	EViewportType ViewportType = EViewportType::Perspective;
	FViewSettings ViewSettings;
	D3D11_VIEWPORT ViewportInfo;
	bool bDrawEditorGizmos = true;
};