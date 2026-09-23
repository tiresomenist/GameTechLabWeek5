#include "pch.h"
#include "Editor.h"

#include "Engine/Component/CameraComponent.h"
#include "Engine/Actor/Actor.h"

#include "Editor/Window/EditorWindow.h"
#include "Editor/Window/ConsoleWindow.h"
#include "Editor/Window/PropertyWindow.h"
#include "Editor/Window/PlaceActorWindow.h"
#include "Editor/Window/OutlinerWindow.h"
#include "Editor/Window/AssetBrowserWindow.h"
#include "Editor/Window/DebugWindow.h"
#include "Editor/Window/ViewportToolbarWindow.h"

#include "Editor/Gizmo/ObjectAxisGizmo.h"
#include "Editor/Gizmo/WorldAxisGizmo.h"
#include "Editor/Gizmo/WorldGridGizmo.h"

#include "Editor/Grid.h"

#include "Engine/Object/ObjectFactory.h"
#include "Engine/Log.h"

#include "Engine/Input/InputManager.h"

#include "Editor/Picker/ObjectPicker.h"
#include "Editor/Picker/GizmoPicker.h"
#include "Engine/Component/WidgetComponent.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Engine/Memory/Allocator.h"

#include "Engine/Scene/SceneManager.h"

#include "Engine/Scene/Scene.h"

#include "Core/Util/File.h"

#include <charconv>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <system_error>
#include <psapi.h>

namespace
{
	constexpr char EditorSettingsFileName[] = "editor.ini";

	FStringView TrimIniWhitespace(FStringView Text)
	{
		const auto First = Text.find_first_not_of(" \t\r\n");
		if (First == FStringView::npos)
		{
			return {};
		}
		const auto Last = Text.find_last_not_of(" \t\r\n");
		return Text.substr(First, Last - First + 1);
	}

	bool TryReadIniValue(FStringView Text, FStringView Section, FStringView Key, FStringView& OutValue)
	{
		// UTF-8 BOM이 존재하는 경우 삭제
		if (Text.starts_with("\xEF\xBB\xBF"))
		{
			Text.remove_prefix(3);
		}

		FStringView CurrentSection;
		FStringView FoundValue;
		bool bFound = false;

		while (!Text.empty())
		{
			// 현재 줄과 이후 내용을 분리함
			const auto Newline = Text.find('\n');
			FStringView Line;

			if (Newline == FStringView::npos)
			{
				Line = Text;
				Text = {};
			}
			else
			{
				Line = Text.substr(0, Newline);
				Text.remove_prefix(Newline + 1);
			}

			Line = TrimIniWhitespace(Line);

			// 빈 줄과 전체 줄 주석을 무시함
			if (Line.empty()||Line.front() == ';'||Line.front() == '#')
			{
				continue;
			}

			// 현재 섹션을 갱신함
			if (Line.front() == '[')
			{
				CurrentSection = {};

				if (Line.size() >= 2 && Line.back() == ']')
				{
					CurrentSection = TrimIniWhitespace(Line.substr(1, Line.size() - 2));
				}

				continue;
			}

			if (CurrentSection != Section)
			{
				continue;
			}

			// 키와 값을 첫 번째 등호를 기준으로 분리함
			const auto EqualPosition = Line.find('=');

			if (EqualPosition == FStringView::npos)
			{
				continue;
			}

			const FStringView LineKey = TrimIniWhitespace(Line.substr(0, EqualPosition));

			if (LineKey != Key)
			{
				continue;
			}

			FoundValue = TrimIniWhitespace(Line.substr(EqualPosition + 1));

			// 동일한 키가 중복되면 마지막 값을 사용함
			bFound = true;
		}

		if (!bFound)
		{
			return false;
		}

		OutValue = FoundValue;
		return true;
	}

	bool TryReadIniFloat(FStringView Text,FStringView Section,FStringView Key,float& OutValue)
	{
		FStringView ValueText;

		if (!TryReadIniValue(Text, Section, Key, ValueText))
		{
			return false;
		}

		float ParsedValue = 0.0f;
		bool bValid = false;

		if (!ValueText.empty())
		{
			const char* Begin = ValueText.data();
			const char* End = Begin + ValueText.size();

			const auto Result = std::from_chars(Begin, End,ParsedValue,std::chars_format::general);

			bValid = (Result.ec==std::errc{})&&(Result.ptr == End)&&(std::isfinite(ParsedValue));
		}

		if (!bValid)
		{
			UE_LOG("설정 숫자 형식 오류: [{}] {}={}",Section,Key,ValueText);
			return false;
		}

		OutValue = ParsedValue;
		return true;
	}

	bool TryReadIniBool(FStringView Text, FStringView Section, FStringView Key, bool& OutValue)
	{
		FStringView ValueText;

		if (!TryReadIniValue(Text, Section, Key, ValueText))
		{
			return false;
		}

		if (ValueText == "true" || ValueText == "1")
		{
			OutValue = true;
			return true;
		}

		if (ValueText == "false" || ValueText == "0")
		{
			OutValue = false;
			return true;
		}

		UE_LOG("설정 Bool 형식 오류: [{}] {}={}",Section,Key,ValueText);

		return false;
	}
	const char* GetViewModeName(EViewModeIndex Mode)
	{
		for (const FViewModeEntry& Entry : ViewModeEntries)
		{
			if (Entry.Mode == Mode)
			{
				return Entry.Name;
			}
		}
		return nullptr;
	}

	bool TryParseViewMode(FStringView Name, EViewModeIndex& OutMode)
	{
		for (const FViewModeEntry& Entry : ViewModeEntries)
		{
			if (Name == FStringView(Entry.Name))
			{
				OutMode = Entry.Mode;
				return true;
			}
		}
		return false;
	}

	struct FShowFlagIniEntry
	{
		const char* Key;
		EEngineShowFlag Flag;
	};

	// INI 키와 내부 ShowFlag의 대응 관계
	constexpr FShowFlagIniEntry ShowFlagIniEntries[] =
	{
		{ "ShowUUID",       EEngineShowFlag::UUID },
		{ "ShowPrimitives", EEngineShowFlag::Primitives },
		{ "ShowGrid",       EEngineShowFlag::Grid },
		{ "ShowBounds",     EEngineShowFlag::Bounds },
		{"ShowWorldAxis",	EEngineShowFlag::WorldAxis},
	};
}

void FEditor::Initialize()
{
	//EditorCamera = static_cast<UCameraComponent*>(SpawnObject(UCameraComponent::GetClass()));
	//EditorCamera->SetRelativeLocation(FVector(-15.0f, -15.0f, 10.0f));
	//EditorCamera->LookAt(FVector(0.0f, 0.0f, 0.0f));
	
	// TODO: 뷰포트 순서 하드코딩 되어있는 거 열거형으로 리팩토링
	FViewportClient PerspectiveView;
	UCameraComponent* PerspectiveCamera = static_cast<UCameraComponent*>(SpawnObject(UCameraComponent::GetClass()));
	PerspectiveCamera->SetIsPerspective(true);
	PerspectiveView.Initialize(EViewportType::Perspective, PerspectiveCamera);
	Viewports.Add(PerspectiveView);
	CurrEditedViewportIndex = 0;

	FViewportClient TopView;
	UCameraComponent* TopCamera = static_cast<UCameraComponent*>(SpawnObject(UCameraComponent::GetClass()));
	TopCamera->SetIsPerspective(false);
	TopView.Initialize(EViewportType::Top, TopCamera);
	Viewports.Add(TopView);

	FViewportClient FrontView;
	UCameraComponent* FrontCamera = static_cast<UCameraComponent*>(SpawnObject(UCameraComponent::GetClass()));
	FrontCamera->SetIsPerspective(false);
	FrontView.Initialize(EViewportType::Front, FrontCamera);
	Viewports.Add(FrontView);

	FViewportClient RightView;
	UCameraComponent* RightCamera = static_cast<UCameraComponent*>(SpawnObject(UCameraComponent::GetClass()));
	RightCamera->SetIsPerspective(false);
	RightView.Initialize(EViewportType::Right, RightCamera);
	Viewports.Add(RightView);

	// 기본으로 PerspectiveCamera 설정
	EditorCamera = PerspectiveCamera;
	CameraController.SetCamera(PerspectiveCamera);
	CameraController.SetViewportClient(&Viewports[0]);
	
	// 스플리터 초기화
	SWindow* WinPerspective = new SWindow;
	WinPerspective->SetViewportClient(&Viewports[0]);
	SWindow* WinTop = new SWindow;
	WinTop->SetViewportClient(&Viewports[1]);
	SWindow* WinFront = new SWindow;
	WinFront->SetViewportClient(&Viewports[2]);
	SWindow* WinRight = new SWindow;
	WinRight->SetViewportClient(&Viewports[3]);

	SSplitterV* LeftSplitter = new SSplitterV;
	LeftSplitter->SideLT = WinTop;
	LeftSplitter->SideRB = WinFront;

	SSplitterV* RightSplitter = new SSplitterV;
	RightSplitter->SideLT = WinPerspective;
	RightSplitter->SideRB = WinRight;
	
	RootSplitter = new SSplitterH;
	RootSplitter->SideLT = LeftSplitter;
	RootSplitter->SideRB = RightSplitter;

	// 초기 뷰포트 크기 설정
	const auto& EngineViewport = GEngine::GetInstance()->GetViewport();
	OnResize(EngineViewport.Width, EngineViewport.Height);

	ObjectPicker = new FObjectPicker(this);
	GizmoPicker = new FGizmoPicker(this);

	MenuLayout.Initialize(this);

	InitializeGizmos();
	GizmoController = new FGizmoController(this);
	InitializeWindows();
	InitializeGrids();
	LoadEditorSetting();

	bInitialized = true;
}

void FEditor::InitializeGizmos()
{
	RegisterGizmo(UObjectAxisGizmo::GetClass());
	RegisterGizmo(UWorldAxisGizmo::GetClass());
	RegisterGizmo(UWorldGridGizmo::GetClass());
}

void FEditor::InitializeWindows()
{
	RegisterWindow(UAssetBrowserWindow::GetClass(), "Asset Browser");
	RegisterWindow(UConsoleWindow::GetClass(), "Console");
	RegisterWindow(UDebugWindow::GetClass(), "Debug");
	RegisterWindow(UPropertyWindow::GetClass(), "Properties");
	RegisterWindow(UPlaceActorWindow::GetClass(), "Place Actors");
	RegisterWindow(UOutlinerWindow::GetClass(), "Outliner");
	RegisterWindow(UViewportToolbarWindow::GetClass(), "Viewport Toolbar");
}

void FEditor::InitializeGrids()
{
	RegisterGrid(UGrid::GetClass());
}

void FEditor::Tick(float DeltaTime)
{
	ApplyPendingSceneCamera();

	//CameraController.Tick(DeltaTime);
	GEngine& Engine = *GEngine::GetInstance();
	GInputManager& Input = *GInputManager::GetInstance();

	ImGuiIO& IO = ImGui::GetIO();
	bool bWantToCaptureMouse = IO.WantCaptureMouse;
	bool bWantToCaptureKeyboard = IO.WantCaptureKeyboard;

	float Time = Engine.GetTime();
	const bool bWasDragging = GizmoController->IsDragging();

	const bool bLDown = Input.GetKey(GInputManager::EI_LMOUSE);
	const bool bRDown = Input.GetKey(GInputManager::EI_RMOUSE);

	// 드래그중이 아니고 처음 눌린 순간인지 판단
	const bool bLFirstPressed = bLDown && !bPrevLDown;
	const bool bRFirstPressed = bRDown && !bPrevRDown;

	ImVec2 MousePos = IO.MousePos;
	FPoint MouseCoord{ MousePos.x, MousePos.y };
	
	bool bIsLeftClick = Input.ConsumeLeftClick();

	if (RootSplitter)
	{
		RootSplitter->OnMouseMove(MouseCoord);
	}

	// 스플리터 클릭시 클릭소모
	if (!IO.WantCaptureMouse && bIsLeftClick)
	{
		if (RootSplitter && RootSplitter->OnMouseDown(MouseCoord))
		{
			return;
		}
	}

	// 마우스 떼면 스플리터 드래그 종료
	if (!bLDown && RootSplitter)
	{
		RootSplitter->OnMouseUp(MouseCoord);
	}


	// 뷰포트 선택
	if (!bWasDragging && !bWantToCaptureMouse && (bLFirstPressed || bRFirstPressed) && CurrMaxViewIdx == -1)
	{	// 드래깅 중, ui 조작 중에는 새로운 뷰포트 선택X
		const float x = bLFirstPressed ? Input.GetLeftCursorPixelX() : Input.GetRightCursorPixelX();
		const float y = bLFirstPressed ? Input.GetLeftCursorPixelY() : Input.GetRightCursorPixelY();
		
		for (uint32 i = 0; i < Viewports.Num(); ++i)
		{
			if (Viewports[i].IsMouseInside(x, y) && CurrEditedViewportIndex != i)
			{
				EditorCamera = Viewports[i].GetCamera();
				CameraController.SetCamera(EditorCamera);
				CameraController.SetViewportClient(&Viewports[i]);
				CurrEditedViewportIndex = i;	// 현재 인덱스 저장
				break;
			}
		}
	}

	if (bIsLeftClick &&!bWasDragging &&!bWantToCaptureMouse &&!Input.GetKey(GInputManager::EI_RMOUSE))
	{
		D3D11_VIEWPORT currViewport = Viewports[CurrEditedViewportIndex].GetRenderView().Viewport;
		int32 SelectedGizmo = GizmoPicker->Pick(ObjectAxisGizmo, currViewport);
		//기즈모가 선택되면 드래그 시작
		if (SelectedGizmo != -1) {
			if (Input.GetKey(GInputManager::EI_LMOUSE))
				GizmoController->BeginDrag(SelectedGizmo);
		}
		//기즈모가 선택 안되면 오브젝트 선택
		else
		{
			USceneComponent* Selected = ObjectPicker->Pick();

			SetSelectedComponent(Selected);
			if (Selected != nullptr) {
				UE_LOG("[{}] : [{}번째 오브젝트 선택]", Time, Selected->GetUUID());
			}
		}
	}

	const bool bGizmoOwnsInput = bWasDragging || GizmoController->IsDragging();

	GizmoController->Tick();
	if (bGizmoOwnsInput|| bWantToCaptureMouse)
	{
		// 카메라를 막는 동안 쌓인 회전 입력 폐기
		int32 DX, DY;
		Input.ConsumeRightDragDelta(DX, DY);
	}

	bool bRightClickDragging = Input.GetKey(GInputManager::EI_RMOUSE);
	bool bAllowCameraMouse = !bWantToCaptureMouse;
	bool bAllowCameraKeyboard = !bWantToCaptureKeyboard || (bAllowCameraMouse && bRightClickDragging);

	if (!bGizmoOwnsInput && bAllowCameraKeyboard && bAllowCameraMouse)
	{
		CameraController.Tick(DeltaTime);
	}
	const bool bSpacePressed = Input.ConsumeSpacePress();
	if (bSpacePressed && !IO.WantCaptureKeyboard)
	{
		GizmoController->ChangeMod();
	}

	bPrevLDown = bLDown;
	bPrevRDown = bRDown;
}

void FEditor::Release()
{
	if (bInitialized) {
		bInitialized = false;
		try
		{
			SaveEditorSetting();
		}
		catch (const std::exception& Exception)
		{
			UE_LOG("에디터 설정 저장 실패:{}", Exception.what());
		}
	}
	
	delete GizmoController;
	GizmoController = nullptr;

	CameraController.SetCamera(nullptr);
	CameraController.SetViewportClient(nullptr);

	delete ObjectPicker;
	ObjectPicker = nullptr;

	delete GizmoPicker;
	GizmoPicker = nullptr;

	SelectedActor = nullptr;
	SelectedComponent = nullptr;

	ReleaseGizmos();
	ReleaseWindows();
	ReleaseGrids();
	ReleaseRootSplitter();
}

void FEditor::ReleaseGizmos()
{
	ObjectAxisGizmo = nullptr;

	for (UGizmo* Gizmo : Gizmos)
	{
		delete Gizmo;
	}
	Gizmos.Empty();
}

void FEditor::ReleaseWindows()
{
	for (UEditorWindow* Window : Windows)
	{
		delete Window;
	}
	Windows.Empty();
}

void FEditor::ReleaseGrids()
{
	for (UGrid* Grid : Grids)
	{
		delete Grid;
	}
	Grids.Empty();
}

void FEditor::ReleaseRootSplitter()
{
	if (RootSplitter) delete RootSplitter;
	RootSplitter = nullptr;
}

void FEditor::SpawnStaticMesh(const FName& MeshKey, int Count)
{
	UScene* CurrentScene = GetCurrentScene();
	if (!CurrentScene || MeshKey.IsNone() || Count <= 0) return;

	for (int i = 0; i < Count; ++i)
	{
		AActor* Actor = nullptr;
		try
		{
			// 선택 상태를 변경하기 전에 Actor와 필요한 컴포넌트를 구성한다.
			Actor = CurrentScene->SpawnActor<AActor*>(AActor::GetClass());
			if (!Actor) throw std::runtime_error("Failed to create a static mesh actor.");

			auto* StaticMeshComp = static_cast<UStaticMeshComponent*>(
				Actor->CreateComponent(UStaticMeshComponent::GetClass()));
			if (!StaticMeshComp) throw std::runtime_error("Failed to create a static mesh component.");

			StaticMeshComp->SetStaticMesh(MeshKey);

			// 기존 Rocket의 정점색 표시 정책을 유지한다.
			if (MeshKey == "Rocket")
			{
				StaticMeshComp->SetOverrideMaterial("Assets/Textures/WhiteTexture.png");
			}

			Actor->CreateComponent(UWidgetComponent::GetClass());
		}
		catch (const std::exception& Error)
		{
			// 씬이 소유한 Actor는 씬의 제거 함수로 정리한다.
			if (Actor) CurrentScene->DestroyActor(Actor);
			UE_LOG("[Editor] Static mesh spawn failed: {}", Error.what());
			return;
		}

		// 구성이 완료된 Actor만 선택 대상으로 공개한다.
		SetSelectedActor(Actor);

	}
}

void FEditor::SpawnComponent(FClassType* ComponentClass, int Count)
{
    if (ComponentClass == nullptr || !ComponentClass->IsA(UActorComponent::GetClass()))
    {
        return;
    }

	UScene* CurrentScene = GetCurrentScene();

	for (int i = 0; i < Count; ++i)
	{
		AActor* Actor = CurrentScene->SpawnActor<AActor*>(AActor::GetClass());
		UActorComponent* Component = Actor->CreateComponent(ComponentClass);

		if (Component != nullptr && Component->IsA(UPrimitiveComponent::GetClass()))
		{
			Actor->CreateComponent(UWidgetComponent::GetClass());
		}

		SetSelectedComponent(Component);
	}
}

void FEditor::CreateEmptyActor()
{
	AActor* Actor = GetCurrentScene()->SpawnActor<AActor*>(AActor::GetClass());
	SetSelectedActor(Actor);
}

void FEditor::NewScene()
{
	// 똑같이 Scene을 불러오되, Deserialize 과정만 생략
	LoadScene("");
}

void FEditor::LoadScene(FStringView SceneName)
{
	CancelWindowRenames();
	SetSelectedActor(nullptr);
	GSceneManager* SceneManager = GSceneManager::GetInstance();
	FSceneType* SceneType = GetCurrentScene()->GetSceneType();

	SceneManager->LoadScene(SceneType, SceneName);

	bPendingCameraLoad = true;
}

void FEditor::LoadSceneFromPath(const std::filesystem::path& ScenePath)
{
	CancelWindowRenames();
	SetSelectedComponent(nullptr);
	GSceneManager* SceneManager = GSceneManager::GetInstance();
	FSceneType* SceneType = GetCurrentScene()->GetSceneType();

	SceneManager->LoadSceneFromPath(SceneType, ScenePath);

	bPendingCameraLoad = true;
}

void FEditor::SaveScene(FStringView SceneName)
{
	GSceneManager* SceneManager = GSceneManager::GetInstance();

	for (const auto& Viewport : Viewports)
	{
		if (Viewport.GetViewportType() == EViewportType::Perspective)
		{
			GetCurrentScene()->SetMainCameraSaveData(Viewport.GetCamera());
			break;
		}
	}

	SceneManager->SaveScene(SceneName);
}

void FEditor::SaveSceneToPath(const std::filesystem::path& ScenePath)
{
	GSceneManager* SceneManager = GSceneManager::GetInstance();
	for (const auto& Viewport : Viewports)
	{
		if (Viewport.GetViewportType() == EViewportType::Perspective)
		{
			GetCurrentScene()->SetMainCameraSaveData(Viewport.GetCamera());
			break;
		}
	}
	SceneManager->SaveSceneToPath(ScenePath);
}

UScene* FEditor::GetCurrentScene()
{
	GSceneManager* SceneManager = GSceneManager::GetInstance();
	return SceneManager->GetScene();
}

void FEditor::SetSelectedActor(AActor* Actor)
{
	SelectedActor = Actor;
	SelectedComponent = nullptr;
	if (GizmoController != nullptr)
	{
		GizmoController->SetSelectedObject(GetTransformTarget());
	}
}

void FEditor::SetSelectedComponent(UActorComponent* Component)
{
	SelectedComponent = Component;
	SelectedActor = Component ? Component->GetOwner() : nullptr;
	if (GizmoController != nullptr)
	{
		GizmoController->SetSelectedObject(GetTransformTarget());
	}
}

USceneComponent* FEditor::GetTransformTarget() const
{
	if (SelectedComponent != nullptr)
	{
		if (SelectedComponent->IsA(USceneComponent::GetClass()))
		{
			return static_cast<USceneComponent*>(SelectedComponent);
		}

		return nullptr;
	}

	if (SelectedActor != nullptr)
	{
		return SelectedActor->GetRootComponent();
	}

	return nullptr;
}

void FEditor::RemoveSelectedComponent()
{
	if (SelectedActor == nullptr || SelectedComponent == nullptr)
	{
		return;
	}
	CancelWindowRenames();
	AActor* Actor = SelectedActor;
	if (Actor->RemoveComponent(SelectedComponent))
	{
		SetSelectedActor(Actor);
	}
}

void FEditor::DeleteSelectedActor()
{
	if (SelectedActor == nullptr) { return; }

	CancelWindowRenames();
	UScene* CurrentScene = GetCurrentScene();
	CurrentScene->DestroyActor(SelectedActor);

	SetSelectedActor(nullptr);
}

void FEditor::RegisterGizmo(FClassType* Type)
{
	UObject* Object = FObjectFactory::ConstructEditorObject(Type);
	UGizmo* Gizmo = static_cast<UGizmo*>(Object);

	Gizmo->Initialize(this);
	if (Gizmo->IsA(UObjectAxisGizmo::GetClass())) {
		SetObjectAxisGizmo(Gizmo);
	}
	Gizmos.Add(Gizmo);
}

void FEditor::RegisterWindow(FClassType* Type, const FString& Name)
{
	UObject* Object = FObjectFactory::ConstructEditorObject(Type);
	UEditorWindow* Window = static_cast<UEditorWindow*>(Object);

	Window->InitializeWindow(this, Name);
	Windows.Add(Window);
}

void FEditor::SetObjectAxisGizmo(UGizmo* InGizmo)
{
	ObjectAxisGizmo = InGizmo;
}

UGizmo* FEditor::GetObjectAxisGizmo() const
{
	return ObjectAxisGizmo;
}

UObject* FEditor::SpawnObject(FClassType* Type)
{
	return FObjectFactory::ConstructEditorObject(Type);
}

void FEditor::OnResize(uint32 Width, uint32 Height, uint32 Left, uint32 Top)
{	// 4개의 뷰포트들의 사이즈를 설정
	// TODO: 고정크기가 아닌 가변 크기로 로직 바꾸어야 함.
	if (Width == 0 || Height == 0) return;

	if (Viewports.IsEmpty()) return;

	if (RootSplitter)
	{
		FRect rect{ static_cast<float>(Left), static_cast<float>(Top), static_cast<float>(Width), static_cast<float>(Height) };
		RootSplitter->UpdateLayout(rect);
	}
}

TArray<FViewportClient>& FEditor::GetViewports()
{
	return Viewports;
}

void FEditor::ToggleMaxView(uint32 InIdx)
{
	if (CurrMaxViewIdx == InIdx)
	{
		CurrMaxViewIdx = -1;
		RootSplitter->SetMaximizeWindow(nullptr);
	}
	else
	{
		CurrMaxViewIdx = InIdx;
		RootSplitter->SetMaximizeWindow(&Viewports[CurrMaxViewIdx]);

		CurrEditedViewportIndex = InIdx;
		EditorCamera = Viewports[InIdx].GetCamera();
		CameraController.SetCamera(EditorCamera);
		CameraController.SetViewportClient(&Viewports[InIdx]);

	}

	const auto& EngineViewport = GEngine::GetInstance()->GetViewport();
	OnResize(EngineViewport.Width, EngineViewport.Height);
}


void FEditor::RegisterGrid(FClassType* Type)
{
	UObject* Object = FObjectFactory::ConstructEditorObject(Type);
	UGrid* Grid = static_cast<UGrid*>(Object);

	Grid->Initialize(this);
	Grids.Add(Grid);
}


//이하 두 함수에서 editor.ini을 읽어서 설정을 저장하거나 불러온다.
void FEditor::LoadEditorSetting()
{
	// 정상적인 로드 여부가 결정되기 전까지 저장을 금지함
	bCanSaveEditorSettings = false;

	if (!EditorCamera) { return; }

	try
	{
		// 파일이 없으면 기본값으로 시작하고 신규 저장을 허용함
		if (!std::filesystem::exists(EditorSettingsFileName))
		{
			bCanSaveEditorSettings = true;
			return;
		}

		const FString FileText = File::ReadText(EditorSettingsFileName);

		// 불러오기가 실패할때 기본값으로 설정하도록 초기화
		float LoadedMoveSpeed = EditorCamera->GetMoveSpeed();
		float LoadedGridInterval = GetGrid().Interval;
		FViewSettings LoadedViewSettings = ViewSettings;

		float ParsedFloat = 0.0f;

		// 카메라 이동 속도를 읽고 양수 여부를 검사함
		if (TryReadIniFloat(FileText,"Camera","MoveSpeed",ParsedFloat))
		{
			if (ParsedFloat > 0.0f)
			{
				LoadedMoveSpeed = ParsedFloat;
			}
			else
			{
				UE_LOG("MoveSpeed 범위 오류. 기존 값 유지: {}",LoadedMoveSpeed);
			}
		}

		// 그리드 간격을 읽고 허용 범위를 검사함
		if (TryReadIniFloat(FileText,"Grid","Interval",ParsedFloat))
		{
			if (ParsedFloat >= FGrid::MinInterval && ParsedFloat <= FGrid::MaxInterval)
			{
				LoadedGridInterval = ParsedFloat;
			}
			else
			{
				UE_LOG("Grid Interval 범위 오류. 기존 값 유지: {}",LoadedGridInterval);
			}
		}

		// 문자열로 저장된 뷰 모드를 복원함
		FStringView ViewModeText;
		for (uint32 i = 0; i < Viewports.Num(); ++i)
		{
			FString SectionName = std::format("Viewport{}", i);//FString::Printf(TEXT("Viewport%d" + i));
			if (TryReadIniValue(FileText, SectionName, "ViewMode", ViewModeText))
			{
				EViewModeIndex ParsedMode = LoadedViewSettings.ViewMode;

				if (TryParseViewMode(ViewModeText, ParsedMode))
				{
					LoadedViewSettings.ViewMode = ParsedMode;
				}
				else
				{
					UE_LOG("알 수 없는 ViewMode: {}. 기존 값 유지함.", ViewModeText);
				}

				// 파일에 존재하는 정상적인 ShowFlag 항목만 변경함
				for (const FShowFlagIniEntry& Entry : ShowFlagIniEntries)
				{
					bool ParsedBool = false;

					if (TryReadIniBool(FileText, SectionName, Entry.Key, ParsedBool))
					{
						LoadedViewSettings.ShowFlags.SetEnabled(Entry.Flag, ParsedBool);
					}
				}
				Viewports[i].SetViewSettings(LoadedViewSettings);
			}
		}

		float RootRatio;
		if (TryReadIniFloat(FileText, "Splitter", "RootRatio", RootRatio))
		{
			RootSplitter->SetSplitRatio(RootRatio);
		}
		float LeftSideRatio;
		if (TryReadIniFloat(FileText, "Splitter", "LeftSideRatio", LeftSideRatio))
		{
			SSplitter* LS = dynamic_cast<SSplitter*>(RootSplitter->GetLTSide());
			LS->SetSplitRatio(LeftSideRatio);

		}
		float RightSideRatio;
		if (TryReadIniFloat(FileText, "Splitter", "RightSideRatio", RightSideRatio))
		{
			SSplitter* RS = dynamic_cast<SSplitter*>(RootSplitter->GetRBSide());
			RS->SetSplitRatio(RightSideRatio);
		}
		// 모든 항목의 해석 완료 후 실제 설정에 적용함
		EditorCamera->SetMoveSpeed(LoadedMoveSpeed);
		SetGridInterval(LoadedGridInterval);
		//ViewSettings = LoadedViewSettings;

		bCanSaveEditorSettings = true;
	}
	catch (const std::exception& Exception)
	{
		UE_LOG("에디터 설정 로드 실패: {}. 기존 설정과 파일을 유지함.",Exception.what());
	}
}

void FEditor::SaveEditorSetting() {
	if (!EditorCamera||!bCanSaveEditorSettings) { return; }
	
	const float MoveSpeed = EditorCamera->GetMoveSpeed();
	const float GridInterval = GetGrid().Interval;
	const char* ViewModeName = GetViewModeName(ViewSettings.ViewMode);
	const float RootSplitterRatio = RootSplitter->GetSplitRatio();
	SSplitter* LeftSpliter = dynamic_cast<SSplitter*>(RootSplitter->GetLTSide());
	const float RootLeftRatio = LeftSpliter->GetSplitRatio();
	SSplitter* RightSpliter = dynamic_cast<SSplitter*>(RootSplitter->GetRBSide());
	const float RootRightRatio = RightSpliter->GetSplitRatio();

	if (!std::isfinite(MoveSpeed) || MoveSpeed <= 0.0f)
	{
		throw std::runtime_error("잘못된 카메라 이동속도를 저장할 수 없음");
	}

	if (!std::isfinite(GridInterval) || GridInterval < FGrid::MinInterval || GridInterval > FGrid::MaxInterval)
	{
		throw std::runtime_error("잘못된 그리드 간격을 저장할 수 없음");
	}

	if (!ViewModeName)
	{
		throw std::runtime_error("알수 없는 뷰모드를 저장할 수 없음");
	}


	//// 현재 설정값을 INI 형식으로 구성함
	FString FileText = std::format(
		"[Camera]\n"
		"MoveSpeed={}\n"
		"\n"
		"[Grid]\n"
		"Interval={}\n",
		EditorCamera->GetMoveSpeed(),
		GetGrid().Interval //,
		//ViewModeName
		//ShowFlags는 어떻게 처리할지 고민좀 해봐야됨
	);

	for (uint32 i = 0; i< Viewports.Num(); ++i)
	{
		const char* ViewModeName = GetViewModeName(Viewports[i].GetViewSettings().ViewMode);
		FileText += std::format(
			"\n[Viewport{}]\n"
			"ViewMode={}" "\n", i, ViewModeName);
		for (const FShowFlagIniEntry& Entry : ShowFlagIniEntries)
		{
			const bool bEnabled = Viewports[i].GetViewSettings().ShowFlags.IsEnabled(Entry.Flag);
			FileText += std::format("{}={}\n", Entry.Key, bEnabled ? "true" : "false");
		}
	}

	//for (const FShowFlagIniEntry& Entry : ShowFlagIniEntries)
	//{
	//	const bool bEnabled = ViewSettings.ShowFlags.IsEnabled(Entry.Flag);
	//	FileText += std::format("{}={}\n",Entry.Key,bEnabled ? "true" : "false");
	//}

	FileText += std::format(
		"\n"
		"[Splitter]\n"
		"RootRatio={}\n"
		"LeftSideRatio={}\n"
		"RightSideRatio={}\n", RootSplitterRatio, RootLeftRatio, RootRightRatio);

	File::WriteText("editor.ini", FileText);
}

static void DrawShadowedText(ImDrawList* DrawList, ImVec2 Pos, ImU32 Color, const char* Text)
{
	DrawList->AddText(ImVec2(Pos.x + 1.0f, Pos.y + 1.0f), IM_COL32(0, 0, 0, 255), Text);
	DrawList->AddText(Pos, Color, Text);
}

void FEditor::DrawStatOverlay()
{
	if (!bShowStatFPS && !bShowStatUnit && !bShowStatMemory) return;

	if (CurrEditedViewportIndex >= Viewports.Num()) return;
	const D3D11_VIEWPORT& D3DView = Viewports[CurrEditedViewportIndex].GetRenderView().Viewport;
	if (D3DView.Width <= 0.0f || D3DView.Height <= 0.0f) return;

	constexpr float ToolbarHeight = 40.0f;
	constexpr float MarginX = 10.0f;
	constexpr float MarginY = 8.0f;
	if (D3DView.Width < 120.0f || D3DView.Height < (ToolbarHeight + 30.0f))
	{
		return;
	}

	ImVec2 MainOrigin = ImGui::GetMainViewport()->Pos;
	float StartX = MainOrigin.x + D3DView.TopLeftX + MarginX;
	float StartY = MainOrigin.y + D3DView.TopLeftY + ToolbarHeight + MarginY;
	ImVec2 ClipMin(MainOrigin.x + D3DView.TopLeftX, MainOrigin.y + D3DView.TopLeftY + ToolbarHeight);
	ImVec2 ClipMax(MainOrigin.x + D3DView.TopLeftX + D3DView.Width, MainOrigin.y + D3DView.TopLeftY + D3DView.Height);

	ImDrawList* DrawList = ImGui::GetForegroundDrawList();
	DrawList->PushClipRect(ClipMin, ClipMax);

	if (bShowStatFPS)    StartY = DrawStatFPS(DrawList, StartX, StartY);
	if (bShowStatUnit)   StartY = DrawStatUnit(DrawList, StartX, StartY);
	if (bShowStatMemory) StartY = DrawStatMemory(DrawList, StartX, StartY);

	DrawList->PopClipRect();
}

float FEditor::DrawStatFPS(ImDrawList* DrawList, float X, float Y)
{
	const FUnitStat& Unit = GEngine::GetInstance()->GetEngineStats().GetUnitStat();
	float Fps = (Unit.FrameTimeMs > 0.0f) ? (1000.0f / Unit.FrameTimeMs) : 0.0f;

	char Buffer[64];
	sprintf_s(Buffer, "%6.2f ms  %5.1f FPS", Unit.FrameTimeMs, Fps);
	DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);

	return Y + 22.0f;
}

float FEditor::DrawStatUnit(ImDrawList* DrawList, float X, float Y)
{
	const FUnitStat& Unit = GEngine::GetInstance()->GetEngineStats().GetUnitStat();
	char Buffer[128];
	constexpr float LineHeight = 18.0f;

	sprintf_s(Buffer, "Game:     %6.2f ms", Unit.GameTimeMs);
	DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
	Y += LineHeight;

	sprintf_s(Buffer, "Draw:     %6.2f ms", Unit.DrawTimeMs);
	DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
	Y += LineHeight;

	sprintf_s(Buffer, "GPU Time: %6.2f ms", Unit.GPUTimeMs);
	DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
	Y += LineHeight;

	sprintf_s(Buffer, "GPU Wait: %6.2f ms", Unit.GPUWaitMs);
	DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
	Y += LineHeight;

	return Y + 6.0f;
}

	float FEditor::DrawStatMemory(ImDrawList * DrawList, float X, float Y)
	{
		const FMemoryStat& Memory = GEngine::GetInstance()->GetEngineStats().GetMemoryStat();
		char Buffer[128];
		constexpr float LineHeight = 18.0f;

		float ProcRamPercent = (Memory.TotalMemMB > 0)
			? (float(Memory.PhysicalMemMB) / float(Memory.TotalMemMB) * 100.0f) : 0.0f;
		sprintf_s(Buffer, "RAM (Proc):  %4zu MB / %zu MB (%.1f%%)",
			Memory.PhysicalMemMB, Memory.TotalMemMB, ProcRamPercent);
		DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
		Y += LineHeight;

		sprintf_s(Buffer, "RAM (Virt):  %4zu MB", Memory.VirtualMemMB);
		DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
		Y += LineHeight;

		float SysRamPercent = (Memory.TotalMemMB > 0)
			? (float(Memory.UsedMemMB) / float(Memory.TotalMemMB) * 100.0f) : 0.0f;
		sprintf_s(Buffer, "RAM (Sys):  %4zu MB / %zu MB (%.1f%%)",
			Memory.UsedMemMB, Memory.TotalMemMB, SysRamPercent);
		DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
		Y += LineHeight;

		float VramPercent = (Memory.GPUDedicatedMemMB > 0)
			? (float(Memory.GPUVRAMUsedMB) / float(Memory.GPUDedicatedMemMB) * 100.0f) : 0.0f;
		sprintf_s(Buffer, "GPU VRAM:    %4zu MB / %zu MB (%.1f%%)",
			Memory.GPUVRAMUsedMB, Memory.GPUDedicatedMemMB, VramPercent);
		DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
		Y += LineHeight;

		sprintf_s(Buffer, "UObjects:  %4zu 개 (%5.2f MB)", Memory.ObjectCount, Memory.HeapBytes / (1024.0f * 1024.0f));
		DrawShadowedText(DrawList, ImVec2(X, Y), IM_COL32(50, 255, 50, 255), Buffer);
		Y += LineHeight;

		return Y + 6.0f;
	}
// 기존 에디터 메뉴와 도킹 레이아웃을 구성합니다.
void FEditor::DrawMenu()
{
	// 현재 사용 중인 메뉴 레이아웃을 그대로 실행합니다.
	MenuLayout.Draw();
}

// 기존 에디터에 등록된 창들을 구성합니다.
void FEditor::DrawWindows(float DeltaTime)
{
	// 스플리터 막대기 렌더링
	if (RootSplitter)
	{
		RootSplitter->Render();
	}

	// 기존 렌더러와 동일한 순서로 창을 처리합니다.
	for (UEditorWindow* Window : Windows)
	{
		Window->Render(DeltaTime);
	}
}

// 일반 에디터는 전체 출력 영역을 씬 뷰포트로 사용합니다.
D3D11_VIEWPORT FEditor::GetRenderViewport(const D3D11_VIEWPORT& FullViewport) const
{
	// 기존 전체 화면 렌더링 영역을 유지합니다.
	return FullViewport;
}

// 일반 에디터는 기존 조작용 기즈모를 표시합니다.
bool FEditor::ShouldDrawEditorGizmos() const
{
	// 기존 렌더러에서 사용하던 기본값을 유지합니다.
	return true;
}

// 일반 에디터는 기존 4분할 뷰포트의 렌더링 정보를 반환합니다.
TArray<FRenderView> FEditor::BuildRenderViews(const D3D11_VIEWPORT& FullViewport) const
{
	// 각 뷰포트가 보유한 카메라, 출력 영역, 표시 설정을 그대로 전달합니다.
	TArray<FRenderView> Views;
	if (CurrMaxViewIdx != -1)
	{
		Views.Add(Viewports[CurrMaxViewIdx].GetRenderView());
		return Views;
	}

	for (const FViewportClient& Viewport : Viewports)
	{
		Views.Add(Viewport.GetRenderView());
	}
	return Views;
}

// 완료된 씬 로드 요청을 확인하여 에디터 카메라를 한 번 복원한다.
void FEditor::ApplyPendingSceneCamera()
{
	if (!bPendingCameraLoad) return;

	const ESceneLoadResult Result =
		GSceneManager::GetInstance()->GetLastLoadResult();

	// 요청이 아직 처리되지 않았다면 다음 Tick에서 다시 확인한다.
	if (Result == ESceneLoadResult::Pending) return;

	// 완료된 요청은 한 번만 처리하고, 실패 시 카메라를 변경하지 않는다.
	bPendingCameraLoad = false;
	if (Result != ESceneLoadResult::Succeeded) return;
	CancelWindowRenames();
	SetSelectedActor(nullptr);
	UScene* Scene = GetCurrentScene();
	if (!Scene) return;
	const FCameraSaveData CameraData = Scene->GetMainCameraSaveData();

	// 저장 대상인 Perspective 뷰의 카메라만 복원한다.
	for (const FViewportClient& Viewport : Viewports)
	{
		if (Viewport.GetViewportType() != EViewportType::Perspective)
			continue;

		UCameraComponent* Camera = Viewport.GetCamera();
		if (!Camera) continue;

		// 실제 뷰의 화면 비율을 포함해 투영 값을 검증하고 함께 적용한다.
		if (!Camera->TrySetProjection(CameraData.FOV, CameraData.NearZ, CameraData.FarZ))
		{
			UE_LOG("[Editor] Saved camera projection cannot be applied to the current viewport.");
			return;
		}

		// 투영 값 적용이 성공한 경우에만 위치와 회전을 변경한다.
		Camera->SetRelativeLocation(CameraData.Location);
		Camera->SetRelativeRotation(CameraData.Rotation);
		break;
	}
}
// 객체 삭제나 씬 전환 전에 이름 편집 대상과 입력 내용을 해제한다.
void FEditor::CancelWindowRenames()
{
	// 닫혀 있는 창에도 편집 대상이 남을 수 있으므로 모든 등록 창을 확인한다.
	for (UEditorWindow* Window : Windows)
	{
		if (Window->IsA(UPropertyWindow::GetClass()))
		{
			static_cast<UPropertyWindow*>(Window)->FinishRename(false);
		}
		else if (Window->IsA(UOutlinerWindow::GetClass()))
		{
			static_cast<UOutlinerWindow*>(Window)->FinishRename(false);
		}
	}
}