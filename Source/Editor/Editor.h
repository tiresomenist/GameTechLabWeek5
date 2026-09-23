#pragma once

#include <algorithm>
#include <cmath>

#include "Core/Container/String.h"
#include "Core/Container/Array.h"
#include "Engine/Object/ObjectFactory.h"
#include "Engine/Renderer/RenderUtil.h"
#include "Editor/Controller/CameraController.h"
#include "Editor/Controller/GizmoController.h"

//TESTCODE//
#include "EditorMenuLayout.h"
#include "Engine/Component/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/Console.h"
#include "Engine/Renderer/ViewSettings.h"

#include "Engine/Renderer/Grid.h"
#include "Core/Name/Name.h"
#include "Engine/Renderer/ViewRenderer.h"
#include "Engine/Renderer/ViewportClient.h"
#include "Editor/UI/Splitter.h"
#include <d3d11.h>

class USceneComponent;
class UCameraComponent;
class AActor;
class UEditorWindow;
class UGizmo;
class UGrid;
class FObjectPicker;
class FGizmoPicker;

class FEditor
{
protected:
	UCameraComponent* EditorCamera = nullptr;
	FCameraController CameraController;
private:
	// 현재 선택된 SceneComponent
	FObjectPicker* ObjectPicker = nullptr;
	FGizmoPicker* GizmoPicker = nullptr;
	FGizmoController* GizmoController = nullptr;

	AActor* SelectedActor = nullptr;
	UActorComponent* SelectedComponent = nullptr;
	FViewSettings ViewSettings;
	FGrid Grid;
	TArray<UGizmo*> Gizmos;
	FEditorMenuLayout MenuLayout;
	TArray<UEditorWindow*> Windows;
	TArray<UGrid*> Grids;
	UGizmo* ObjectAxisGizmo = nullptr;

	// Viewport 배열
	TArray<FViewportClient> Viewports;
	uint32 CurrEditedViewportIndex = 0;
	// 최대화 뷰포트 관련
	int32 CurrMaxViewIdx = -1;

	// 스플리터
	SSplitter* RootSplitter = nullptr;

	//예외처리용 초기화 여부
	bool bInitialized = false;
	bool bCanSaveEditorSettings = false;
	bool bPendingCameraLoad = false;

	// 이전 프레임 마우스 눌림상태 저장 - 드래그 중 뷰포트 변경 방지
	bool bPrevLDown = false;
	bool bPrevRDown = false;

	bool bShowStatUnit = false;
	bool bShowStatMemory = false;
	bool bShowStatFPS = false;

	void InitializeGizmos();
	void InitializeWindows();
	void InitializeGrids();

	void ReleaseGizmos();
	void ReleaseWindows();
	void ReleaseGrids();
	void ReleaseRootSplitter();

	float DrawStatFPS(ImDrawList* DrawList, float X, float Y);
	float DrawStatUnit(ImDrawList* DrawList, float X, float Y);
	float DrawStatMemory(ImDrawList* DrawList, float X, float Y);
	void ApplyPendingSceneCamera();

	// 등록된 Property·Outliner 창에서 진행 중인 이름 편집을 취소한다.
	void CancelWindowRenames();
public:
	virtual ~FEditor() = default;

	virtual void Initialize();

	virtual void Tick(float DeltaTime);

	virtual void Release();

	void SpawnStaticMesh(const FName& MeshKey, int Count);
	void SpawnComponent(FClassType* ComponentClass, int Count);
	void CreateEmptyActor();

	void NewScene();
	void LoadScene(FStringView SceneName);
	void LoadSceneFromPath(const std::filesystem::path& ScenePath);
	void SaveScene(FStringView SceneName);
	void SaveSceneToPath(const std::filesystem::path& ScenePath);

	virtual UScene* GetCurrentScene();
	UCameraComponent* GetEditorCamera() { return EditorCamera; }
	const FViewSettings& GetViewSettings()const { return ViewSettings; }

	void SetSelectedActor(AActor* Actor);
	AActor* GetSelectedActor() const { return SelectedActor; }
	void SetSelectedComponent(UActorComponent* Component);
	UActorComponent* GetSelectedComponent() const { return SelectedComponent; }
	USceneComponent* GetSelectedSceneComponent() const { return dynamic_cast<USceneComponent*>(SelectedComponent); }
	USceneComponent* GetTransformTarget() const;
	
	//내부적으로 비트마스킹으로 처리해줌.
	bool IsShowingUUIDLabels() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::UUID); }
	void SetShowUUIDLabels(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::UUID, bShow); }
	bool IsShowingBoundingBoxes() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Bounds); }
	void SetShowBoundingBoxes(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::Bounds, bShow); }
	EViewModeIndex GetViewMode() const { return ViewSettings.ViewMode; }
	void SetViewMode(EViewModeIndex InMode) { ViewSettings.ViewMode = InMode; }
	bool IsShowingPrimitives() const{return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Primitives);}
	void SetShowPrimitives(bool bShow){ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::Primitives, bShow);}
	bool IsShowingGrid() const{return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Grid);}
	void SetShowGrid(bool bShow){ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::Grid, bShow);}
	bool IsShowingWorldAxis() const { return ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::WorldAxis); }
	void SetShowWorldAxis(bool bShow) { ViewSettings.ShowFlags.SetEnabled(EEngineShowFlag::WorldAxis, bShow); }
	const FGrid& GetGrid() const { return Grid; }
	void SetGridInterval(float InInterval)
	{
		if (std::isfinite(InInterval))
		{
			Grid.Interval = std::clamp(InInterval, FGrid::MinInterval, FGrid::MaxInterval);
		}
	}
	int32 GetActiveGizmoAxis() const { return GizmoController ? GizmoController->GetActiveAxis() : -1; }

	void RemoveSelectedComponent();
	void DeleteSelectedActor();

	void RegisterGizmo(FClassType* Type);
	void RegisterWindow(FClassType* Type, const FString& Name);
	void RegisterGrid(FClassType* Type);

	void LoadEditorSetting();

	void SaveEditorSetting();

	const TArray<UGizmo*>& GetGizmos() const { return Gizmos; }
	void DrawLayout() { return MenuLayout.Draw(); }
	const TArray<UEditorWindow*>& GetWindows() const { return Windows; }
	const TArray<UGrid*>& GetGrids() const { return Grids; }

	//TEST CODE//
	FVector GetCameraLocation() { return GetEditorCamera()->GetRelativeLocation(); }
	void SetCameraLocation(FVector NewCameraLocation) { EditorCamera->SetRelativeLocation(NewCameraLocation); }
	FVector GetCameraRotationDegree()
	{
		const FRotator& CameraRotation = GetEditorCamera()->GetRelativeRotator();
		return CameraRotation.ToEulerDegrees();
	}
	void SetCameraRotationDegree(const FVector& NewRotationDegree)
	{
		GetEditorCamera()->SetRelativeRotation(FRotator::FromEulerDegrees(NewRotationDegree));
	}
	float GetCameraFOV() { return GetEditorCamera()->GetFOV() * 180.0f / PI; }
	void SetCameraFOV(float NewFOV) { EditorCamera->SetFOVByDegree(NewFOV); }
	
	void SpawnPrimitives(FClassType* ClassType, uint32 num) { GEngine::GetInstance()->GetConsole()->Append(std::format("Make {}, {} times",ClassType->DisplayName,num)); }
	
	void SetObjectAxisGizmo(UGizmo* InGizmo);
	UGizmo* GetObjectAxisGizmo()const;

	UObject* SpawnObject(FClassType* Type);

	// 뷰포트 사이즈 설정용 함수
	void OnResize(uint32 Width, uint32 Height, uint32 Left = 0, uint32 Top = 0);

	TArray<FViewportClient>& GetViewports();
	const uint32 GetCurrentEditViewportIndex() { return CurrEditedViewportIndex; }
	const uint32 GetCurrMaxViewIdx() { return CurrMaxViewIdx; }
	void ToggleMaxView(uint32 InIdx);

	// 스플리터
	SSplitter* GetSplitter() const { return RootSplitter; }

	bool IsShowingStatUnit() const { return bShowStatUnit; }
	void SetShowStatUnit(bool bShow) { bShowStatUnit = bShow; }
	void ToggleShowStatUnit() { bShowStatUnit = !bShowStatUnit; }

	bool IsShowingStatFPS() const { return bShowStatFPS; }
	void SetShowStatFPS(bool bShow) { bShowStatFPS = bShow; }
	void ToggleShowStatFPS() { bShowStatFPS = !bShowStatFPS; }

	bool IsShowingStatMemory() const { return bShowStatMemory; }
	void SetShowStatMemory(bool bShow) { bShowStatMemory = bShow; }
	void ToggleShowStatMemory() { bShowStatMemory = !bShowStatMemory; }

	void HideAllStats() { bShowStatFPS = bShowStatUnit = bShowStatMemory = false; }
	void ShowAllStats() { bShowStatFPS = bShowStatUnit = bShowStatMemory = true; }

	void DrawStatOverlay();

	// 씬 렌더링 전에 메뉴와 화면 배치를 구성합니다.
	virtual void DrawMenu();

	// 씬 렌더링 후 에디터 창의 UI를 구성합니다.
	virtual void DrawWindows(float DeltaTime);

	// 전체 출력 영역에서 실제 씬을 그릴 영역을 반환합니다.
	virtual D3D11_VIEWPORT GetRenderViewport(const D3D11_VIEWPORT& FullViewport) const;

	// 오브젝트 조작용 기즈모의 표시 여부를 반환합니다.
	virtual bool ShouldDrawEditorGizmos() const;

	// 이번 프레임에 렌더링할 카메라와 뷰포트 목록을 구성합니다.
	virtual TArray<FRenderView> BuildRenderViews(const D3D11_VIEWPORT& FullViewport) const;


public:
	friend TArray<FPrimitiveRenderData> RenderUtil::GetRenderList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera);
	friend TArray<FPrimitiveRenderData> RenderUtil::GetGizmoList(FEditor* Editor, UScene* Scene,const UCameraComponent* Camera,
		const D3D11_VIEWPORT& Viewport);
};
