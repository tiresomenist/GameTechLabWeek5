#include "pch.h"
#include "ObjViewer.h"
#include "Editor/Grid.h"
#include "Editor/Gizmo/WorldAxisGizmo.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Input/InputManager.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "Core/Util/File.h"
#include "Engine/Log.h"
#include <cwchar>
#include <exception>
#include "Engine/Actor/Actor.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Engine/Resource/ResourceManager.h"
#include <cmath>
#include <stdexcept>
#include <format>
#include "Engine/Component/CameraComponent.h"
#include <algorithm>
#include "Core/Math/Quaternion.h"
#include "Editor/Window/AssetBrowserWindow.h"

#if defined(OBJVIEWER_APP)
// 기존 메시 렌더링을 재사용하며 Viewer의 섹션 선택만 추가합니다.
class UObjViewerMeshComponent : public UStaticMeshComponent
{
    UCLASS(UObjViewerMeshComponent, "ObjViewerMeshComponent", UStaticMeshComponent)

public:
    // 선택한 섹션과 단독 표시 여부를 함께 설정합니다.
    void SetSectionDisplay(int32 InSectionIndex, bool bInOnlySelectedSection)
    {
        SelectedSectionIndex = InSectionIndex;
        bOnlySelectedSection = bInOnlySelectedSection;
    }

    // 선택한 섹션의 렌더 데이터에 외곽선 표시를 요청합니다.
    // 기존 섹션 렌더 데이터를 생성하고 표시할 섹션만 남깁니다.
    void CreateRenderData(TArray<FPrimitiveRenderData>& OutData, bool bSelected) override
    {
        // 부모 함수가 이번 컴포넌트의 렌더 데이터를 추가한 범위를 구합니다.
        const int32 FirstNewIndex = OutData.Num();
        Super::CreateRenderData(OutData, false);
        const int32 SectionCount = OutData.Num() - FirstNewIndex;
        const bool bValidSelection = SelectedSectionIndex >= 0
            && SelectedSectionIndex < SectionCount;
        const bool bFilterSections = bOnlySelectedSection && bValidSelection;

        // 유지할 데이터를 앞쪽으로 옮기며 선택한 섹션에 강조 표시를 설정합니다.
        int32 WriteIndex = FirstNewIndex;
        for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
        {
            const bool bSectionSelected = bValidSelection && SectionIndex == SelectedSectionIndex;
            if (bFilterSections && !bSectionSelected) continue;

            FPrimitiveRenderData Data = OutData[FirstNewIndex + SectionIndex];
            Data.isSelected = bSectionSelected;
            Data.bAllowOutline = true;
            OutData[WriteIndex++] = Data;
        }

        // 앞에서 유지한 데이터까지만 렌더러에 전달합니다.
        OutData.SetNum(WriteIndex);
    }

private:
    int32 SelectedSectionIndex = -1;
    bool bOnlySelectedSection = false;
};
#endif


// Viewer 전용 씬, 카메라, 그리드와 월드축을 생성합니다.
void FObjViewer::Initialize()
{
    // 에디터의 ImGui 배치 파일을 읽거나 덮어쓰지 않도록 설정합니다.
    ImGui::GetIO().IniFilename = nullptr;

    // 카메라는 PreviewScene의 Actor가 소유하고 EditorCamera는 참조만 합니다.
    PreviewScene = new UScene();
    PreviewScene->CreateMainCamera();
    EditorCamera = PreviewScene->GetMainCamera();
    EditorCamera->SetIsPerspective(true);
    EditorCamera->SetRelativeLocation(FVector(-15.0f, -15.0f, 10.0f));
    EditorCamera->LookAt(FVector(0.0f, 0.0f, 0.0f));
    CameraController.SetCamera(EditorCamera);

    // 모델 미리보기에 사용할 기본 표시 설정을 구성합니다.
    SetViewMode(EViewModeIndex::VMI_Unlit);
    SetShowPrimitives(true);
    SetShowGrid(true);
    SetShowWorldAxis(true);
    SetShowUUIDLabels(false);
    SetShowBoundingBoxes(false);

    // 기존 등록 기능과 라인 렌더링 경로를 재사용합니다.
    RegisterGrid(UGrid::GetClass());
    RegisterGizmo(UWorldAxisGizmo::GetClass());

    // 공통 에셋 브라우저를 Viewer의 창 목록에 등록합니다.
    RegisterWindow(UAssetBrowserWindow::GetClass(), "Asset Browser");
    // 공통 창의 기본 동작은 유지하고 Viewer 인스턴스에만 옵션을 적용합니다.
    for (UEditorWindow* Window : GetWindows())
    {
        if (!Window->IsA(UAssetBrowserWindow::GetClass())) continue;
        auto* Browser = static_cast<UAssetBrowserWindow*>(Window);
        Browser->SetObjOnly(true);
        Browser->SetOnObjActivated([this](const std::filesystem::path& Path)
        {
            PendingObjPath = Path;
        });
    }
    PreviewScene->BeginPlay();
}

// Viewer 씬을 갱신하고 기존 카메라 컨트롤러에 입력을 전달합니다.
void FObjViewer::Tick(float DeltaTime)
{
    if (!PreviewScene || !EditorCamera) return;
    // ImGui 창 렌더링이 끝난 다음 프레임에 모델을 교체합니다.
    if (!PendingObjPath.empty())
    {
        std::filesystem::path Path;
        Path.swap(PendingObjPath);
        OpenObjPath(Path);
        return;
    }
    // 파일 대화상자를 닫은 프레임에는 카메라 입력을 처리하지 않습니다.
    if (bOpenObjDialogRequested)
    {
        bOpenObjDialogRequested = false;
        OpenObjDialog();
        return;
    }
    PreviewScene->Tick(DeltaTime);
    // 현재 Viewer에서 사용하지 않는 에디터 선택 입력을 소비합니다.
    GInputManager& Input = *GInputManager::GetInstance();
    Input.ConsumeLeftClick();
    Input.ConsumeSpacePress();

    // 입력 차단 여부와 화면 크기에 따른 보정은 전용 함수에서 처리합니다.
    UpdateOrbitCamera(DeltaTime);
}

// 공통 등록 자원을 정리한 뒤 Viewer 씬을 제거합니다.
void FObjViewer::Release()
{
    // 컨트롤러의 카메라 참조와 등록한 그리드·기즈모를 먼저 정리합니다.
    FEditor::Release();
    PendingObjPath.clear();
    EditorCamera = nullptr;
    PreviewActor = nullptr;
    PreviewComponent = nullptr;
    PreviewMesh = nullptr;
    SelectedSectionIndex = -1;
    bOnlySelectedSection = false;
    // 씬을 삭제하면 소유 Actor와 카메라 컴포넌트도 함께 삭제됩니다.
    if (PreviewScene)
    {
        PreviewScene->EndPlay();
        delete PreviewScene;
        PreviewScene = nullptr;
    }
}

// 렌더러와 공통 기능에 Viewer 전용 씬을 제공합니다.
UScene* FObjViewer::GetCurrentScene()
{
    return PreviewScene;
}

// 파일 선택 메뉴와 기존 표시 설정 메뉴를 구성합니다.
void FObjViewer::DrawMenu()
{
    MenuBarHeight = 0.0f;
    if (!ImGui::BeginMainMenuBar())
    {
        DrawDockLayout();
        return;
    }
    MenuBarHeight = ImGui::GetWindowHeight();

    // 대화상자는 다음 Tick에서 열고 현재 ImGui 프레임은 정상적으로 마무리합니다.
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open OBJ...")) { bOpenObjDialogRequested = true; }
        ImGui::EndMenu();
    }

    // 표시 설정과 등록된 도구 창의 열림 상태를 변경합니다.
    if (ImGui::BeginMenu("View"))
    {
        bool bShowGrid = IsShowingGrid();
        if (ImGui::MenuItem("Grid", nullptr, &bShowGrid)) { SetShowGrid(bShowGrid); }

        bool bShowWorldAxis = IsShowingWorldAxis();
        if (ImGui::MenuItem("World Axis", nullptr, &bShowWorldAxis)) { SetShowWorldAxis(bShowWorldAxis); }

        // 기존 뷰 모드 선택을 유지합니다.
        ImGui::Separator();
        for (const FViewModeEntry& Entry : ViewModeEntries)
        {
            if (ImGui::MenuItem(Entry.Name, nullptr, GetViewMode() == Entry.Mode))
                SetViewMode(Entry.Mode);
        }

        // 창이 사용하는 bOpen을 직접 연결하여 닫기와 다시 열기를 지원합니다.
        ImGui::Separator();
        for (UEditorWindow* Window : GetWindows())
        {
            ImGui::PushID(Window);
            ImGui::MenuItem(Window->GetWindowName().c_str(), nullptr, Window->GetOpenPtr());
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }

    // 현재 선택 상태를 표시하며 파일명에 포함된 서식 문자를 해석하지 않습니다.
    ImGui::Separator();
    if (!FileSelectionError.empty())
    {
        ImGui::TextUnformatted(FileSelectionError.c_str());
    }
    else if (!SelectedObjName.empty())
    {
        ImGui::TextUnformatted(SelectedObjName.c_str());
    }
    else
    {
        ImGui::TextUnformatted("No OBJ selected");
    }

    ImGui::EndMainMenuBar();
    DrawDockLayout();
}

// 중앙 모델 영역과 오른쪽 정보 창, 하단 에셋 창의 도킹 배치를 구성합니다.
void FObjViewer::DrawDockLayout()
{
    const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
    if (MainViewport->WorkSize.x <= 0.0f || MainViewport->WorkSize.y <= 0.0f) return;
    ViewerDockSpaceId = ImHashStr("ObjViewerDockSpace");
    constexpr ImGuiDockNodeFlags Flags = ImGuiDockNodeFlags_PassthruCentralNode
        | ImGuiDockNodeFlags_NoDockingOverCentralNode;

    // 첫 생성 때만 나누어 사용자가 조절한 도킹 경계를 유지합니다.
    if (!ImGui::DockBuilderGetNode(ViewerDockSpaceId))
    {
        ImGui::DockBuilderAddNode(ViewerDockSpaceId, ImGuiDockNodeFlags_DockSpace | Flags);
        ImGui::DockBuilderSetNodePos(ViewerDockSpaceId, MainViewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(ViewerDockSpaceId, MainViewport->WorkSize);
        ImGuiID CenterId = ViewerDockSpaceId;
        const float InfoRatio = (std::min)(320.0f / MainViewport->WorkSize.x, 0.35f);
        const ImGuiID InfoId = ImGui::DockBuilderSplitNode(
            CenterId, ImGuiDir_Right, InfoRatio, nullptr, &CenterId);
        const ImGuiID AssetsId = ImGui::DockBuilderSplitNode(
            CenterId, ImGuiDir_Down, 0.30f, nullptr, &CenterId);
        ImGui::DockBuilderDockWindow("Model Info", InfoId);
        ImGui::DockBuilderDockWindow("Asset Browser", AssetsId);
        ImGui::DockBuilderFinish(ViewerDockSpaceId);
    }
    // 중앙 배경을 투명하게 하여 DirectX 모델 화면을 노출합니다.
    ImGui::DockSpaceOverViewport(ViewerDockSpaceId, MainViewport, Flags);
}

// 등록된 공통 창과 Viewer의 모델 정보 패널을 표시합니다.
void FObjViewer::DrawWindows(float DeltaTime)
{
    // 모델 로딩 여부와 관계없이 에셋 브라우저를 먼저 처리합니다.
    FEditor::DrawWindows(DeltaTime);

    // 초기 DockBuilder 설정을 읽도록 허용합니다. 파일 저장은 IniFilename=nullptr로 차단합니다.
    constexpr ImGuiWindowFlags Flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Model Info", nullptr, Flags))
    {
        ImGui::End();
        return;
    }

    // 모델이 없는 상태에서도 파일을 여는 방법을 안내합니다.
    if (!PreviewMesh)
    {
        ImGui::TextWrapped("Double-click an OBJ in Asset Browser or use File > Open OBJ.");
        ImGui::End();
        return;
    }

    const FMeshResource* Resource = PreviewMesh->GetMeshResource();
    const TArray<FMeshSection>& Sections = PreviewMesh->GetSections();

    // 공유 메시의 원본 정보를 읽으며 미리보기용 크기 변환은 반영하지 않습니다.
    ImGui::TextWrapped("%s", SelectedObjName.c_str());
    ImGui::Separator();
    ImGui::Text("Vertices: %u", Resource->GetVertexCount());
    ImGui::Text("Triangles: %u", Resource->GetIndexCount() / 3);
    ImGui::Text("Sections: %d", Sections.Num());
    ImGui::Text("Material slots: %d", PreviewMesh->GetDefaultMeshMaterials().Num());

    const FVector OriginalSize = PreviewMesh->GetBoundsMax() - PreviewMesh->GetBoundsMin();
    ImGui::Text("Original size");
    ImGui::Text("X: %.3f  Y: %.3f  Z: %.3f", OriginalSize.X, OriginalSize.Y, OriginalSize.Z);
    ImGui::Separator();

    // 현재 선택한 섹션의 인덱스 범위와 머티리얼 연결 정보를 표시합니다.
    if (SelectedSectionIndex >= 0 && SelectedSectionIndex < Sections.Num())
    {
        const FMeshSection& Section = Sections[SelectedSectionIndex];
        ImGui::Text("Selected section: %d", SelectedSectionIndex);
        ImGui::Text("Triangles: %u", Section.IndexCount / 3);
        ImGui::Text("First index: %u", Section.FirstIndex);
        ImGui::Text("Material slot: %u", Section.MaterialIndex);
        if (Section.ObjectIndex >= 0)
            ImGui::Text("Object index: %d", Section.ObjectIndex);
        else
            ImGui::TextUnformatted("Object: none");

        // 선택한 부위만 확인할 수 있도록 단독 표시 옵션을 제공합니다.
        if (ImGui::Checkbox("Only Selected Section", &bOnlySelectedSection))
            SelectSection(SelectedSectionIndex);

        // 선택을 해제하면 단독 표시도 종료하고 전체 모델을 복원합니다.
        if (ImGui::Button("Clear Selection"))
            SelectSection(-1);
    }
    else
    {
        ImGui::TextUnformatted("No section selected");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Sections");

    // 많은 섹션도 별도 스크롤 영역에서 선택할 수 있도록 구성합니다.
    if (ImGui::BeginChild("SectionList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        for (int32 Index = 0; Index < Sections.Num(); ++Index)
        {
            const FMeshSection& Section = Sections[Index];
            const FString Label = std::format("Section {} | Material {}", Index, Section.MaterialIndex);
            ImGui::PushID(Index);
            if (ImGui::Selectable(Label.c_str(), SelectedSectionIndex == Index))
                SelectSection(Index);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

bool FObjViewer::ShouldDrawEditorGizmos() const
{
    return false;
}

// Viewer 카메라와 전체 출력 영역으로 단일 렌더 뷰를 구성합니다.
TArray<FRenderView> FObjViewer::BuildRenderViews(const D3D11_VIEWPORT& FullViewport) const
{
    TArray<FRenderView> Views;
    if (!PreviewScene || !EditorCamera) return Views;

    // 창 크기에 맞는 출력 영역과 Viewer 표시 설정을 전달합니다.
    FRenderView View{};
    View.Camera = EditorCamera;
    View.Viewport = GetRenderViewport(FullViewport);
    View.ViewSettings = GetViewSettings();
    View.bDrawEditorGizmos = ShouldDrawEditorGizmos();
    if (View.Viewport.Width <= 0.0f || View.Viewport.Height <= 0.0f) return Views;

    Views.Add(View);
    return Views;
}

// 도킹 창을 제외한 중앙 영역을 모델 출력 영역으로 반환합니다.
D3D11_VIEWPORT FObjViewer::GetRenderViewport(const D3D11_VIEWPORT& FullViewport) const
{
    D3D11_VIEWPORT Viewport = FullViewport;

    // 작은 창에서도 음수 크기가 생기지 않도록 메뉴와 패널 크기를 제한합니다.
    const float FullWidth = (std::max)(0.0f, FullViewport.Width);
    const float FullHeight = (std::max)(0.0f, FullViewport.Height);
    // 화면 좌표를 클라이언트 좌표로 바꾸고 최소화 중에도 크기를 제한합니다.
    const ImGuiDockNode* CenterNode = ViewerDockSpaceId != 0
        ? ImGui::DockBuilderGetCentralNode(ViewerDockSpaceId) : nullptr;
    if (CenterNode)
    {
        const ImVec2 Origin = ImGui::GetMainViewport()->Pos;
        const float Left = std::clamp(
            CenterNode->Pos.x - Origin.x - FullViewport.TopLeftX, 0.0f, FullWidth);
        const float Top = std::clamp(
            CenterNode->Pos.y - Origin.y - FullViewport.TopLeftY, 0.0f, FullHeight);
        Viewport.TopLeftX += Left;
        Viewport.TopLeftY += Top;
        Viewport.Width = std::clamp(CenterNode->Size.x, 0.0f, FullWidth - Left);
        Viewport.Height = std::clamp(CenterNode->Size.y, 0.0f, FullHeight - Top);
        return Viewport;
    }
    // 첫 도킹 프레임 전에는 기존 초기 배치를 사용합니다.
    const float ReservedHeight = std::clamp(MenuBarHeight, 0.0f, FullHeight);
    const float PanelWidth = (std::min)(320.0f, FullWidth * 0.35f);

    Viewport.TopLeftY += ReservedHeight;
    Viewport.Height = FullHeight - ReservedHeight;
    Viewport.Width = FullWidth - PanelWidth;
    return Viewport;
}

// OBJ 파일을 선택하고 로딩에 성공한 모델을 미리보기 씬에 연결합니다.
void FObjViewer::OpenObjDialog()
{
    GInputManager& Input = *GInputManager::GetInstance();
    const HWND Owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);

    // 대화상자를 여는 동안 기존 입력 상태가 유지되지 않도록 초기화합니다.
    Input.KillFocus();
    FileSelectionError.clear();

    try
    {
        // 현재 모델의 폴더를 다음 파일 선택의 시작 위치로 사용합니다.
        const std::filesystem::path InitialDirectory = SelectedObjPath.empty()
            ? std::filesystem::path("Assets/Models")
            : SelectedObjPath.parent_path();
        const auto Path = File::OpenFileDialog(Owner, EFileDialogType::Obj, InitialDirectory);

        // 취소하면 기존 모델과 파일명을 유지합니다.
        if (Path)
        {
            OpenObjPath(*Path);
        }
    }
    catch (const std::exception& Error)
    {
        // 대화상자나 시작 폴더 준비 실패를 화면에 표시합니다.
        FileSelectionError = "Could not open the OBJ file dialog.";
        UE_LOG("[ObjViewer] File dialog failed: {}", Error.what());
    }

    // 대화상자 종료 시점에 들어온 입력을 비워 카메라 오작동을 방지합니다.
    Input.KillFocus();
}

// 파일 대화상자와 에셋 브라우저가 동일한 모델 검증 및 교체 경로를 사용합니다.
void FObjViewer::OpenObjPath(const std::filesystem::path& FilePath)
{
    GInputManager& Input = *GInputManager::GetInstance();
    Input.KillFocus();
    FileSelectionError.clear();
    try
    {
        // 절대 경로와 확장자를 확인한 뒤 기존 리소스 로더를 재사용합니다.
        const std::filesystem::path Candidate = std::filesystem::absolute(FilePath).lexically_normal();
        if (_wcsicmp(Candidate.extension().c_str(), L".obj") != 0)
            FileSelectionError = "Please select an OBJ file.";
        else if (!std::filesystem::is_regular_file(Candidate))
            FileSelectionError = "The selected file does not exist.";
        else
            LoadPreviewMesh(Candidate);
    }
    catch (const std::exception& Error)
    {
        // 실패하면 현재 모델을 유지하고 오류를 표시합니다.
        FileSelectionError = "Could not load the OBJ file.";
        UE_LOG("[ObjViewer] OBJ load failed: {}", Error.what());
    }
    Input.KillFocus();
}

// 기존 리소스 로더로 모델을 준비하고 성공한 경우에만 현재 모델을 교체합니다.
void FObjViewer::LoadPreviewMesh(const std::filesystem::path& FilePath)
{
    // 로딩과 표시 이름 준비를 기존 모델 삭제 전에 완료합니다.
    std::filesystem::path NewPath = std::filesystem::absolute(FilePath).lexically_normal();
    const auto Utf8Name = NewPath.filename().u8string();
    FString NewName(Utf8Name.begin(), Utf8Name.end());

    // 기존 로더가 사용하는 문자열 경로와 캐시 키를 그대로 사용합니다.
    const FString PathText = File::PathToUtf8(NewPath);
    const FName MeshKey(PathText);
    UStaticMesh* Mesh = GResourceManager::GetInstance()->GetOrLoadStaticMesh(MeshKey);
    if (!Mesh)
        throw std::runtime_error("Failed to load OBJ mesh.");

    // 로더가 반환했더라도 실제로 그릴 GPU 데이터가 있는지 확인합니다.
    const FMeshResource* Resource = Mesh->GetMeshResource();
    if (!Resource || !Resource->GetVertexBuffer() || !Resource->GetIndexBuffer()
        || Resource->GetIndexCount() == 0 || Mesh->GetSections().IsEmpty() || !Mesh->HasBounds())
        throw std::runtime_error("OBJ contains no renderable mesh.");

    // 원본 데이터는 유지하고 미리보기 컴포넌트의 위치와 크기만 정규화합니다.
    const FVector BoundsMin = Mesh->GetBoundsMin();
    const FVector BoundsMax = Mesh->GetBoundsMax();
    const FVector Center = BoundsMin * 0.5f + BoundsMax * 0.5f;
    const float Radius = (BoundsMax * 0.5f - BoundsMin * 0.5f).Length();
    if (!std::isfinite(Radius) || Radius <= 0.0f)
        throw std::runtime_error("OBJ mesh has invalid spatial extent.");

    const float PreviewScale = PreviewRadius / Radius;
    const FVector PreviewLocation = Center * -PreviewScale;
    if (!std::isfinite(PreviewScale) || !std::isfinite(PreviewLocation.X)
        || !std::isfinite(PreviewLocation.Y) || !std::isfinite(PreviewLocation.Z))
        throw std::runtime_error("OBJ preview transform is out of range.");

    // 새 Actor를 구성하는 동안 기존 모델은 그대로 유지합니다.
    AActor* NewActor = nullptr;
    UStaticMeshComponent* NewComponent = nullptr;
    try
    {
        NewActor = PreviewScene->SpawnActor<AActor*>(AActor::GetClass());
#if defined(OBJVIEWER_APP)
        FClassType* ComponentClass = UObjViewerMeshComponent::GetClass();
#else
        FClassType* ComponentClass = UStaticMeshComponent::GetClass();
#endif
        NewComponent = static_cast<UStaticMeshComponent*>(
            NewActor->CreateComponent(ComponentClass));
        auto* Component = NewComponent;

        // 처음 로딩한 키를 그대로 전달하여 이미 생성된 메시를 재사용합니다.
        Component->SetStaticMesh(MeshKey);
        Component->SetRelativeScale3D(FVector(PreviewScale, PreviewScale, PreviewScale));
        Component->SetRelativeLocation(PreviewLocation);

        // 현재 SpawnActor는 실행 중인 씬에서도 BeginPlay를 자동 호출하지 않습니다.
        NewActor->BeginPlay();
    }
    catch (...)
    {
        // 새 Actor 구성에 실패하면 새 Actor만 제거합니다.
        if (NewActor) PreviewScene->DestroyActor(NewActor);
        throw;
    }

    // 준비가 끝난 뒤 기존 모델을 제거하고 표시 상태를 함께 교체합니다.
    if (PreviewActor) PreviewScene->DestroyActor(PreviewActor);
    PreviewActor = NewActor;
    PreviewComponent = NewComponent;
    PreviewMesh = Mesh;
    SelectSection(-1);
    SelectedObjPath.swap(NewPath);
    SelectedObjName.swap(NewName);
    FileSelectionError.clear();

    // 정규화한 모델 중심을 기본 카메라 위치에서 바라봅니다.
    EditorCamera->SetRelativeLocation(FVector(-15.0f, -15.0f, 10.0f));
    EditorCamera->LookAt(FVector(0.0f, 0.0f, 0.0f));
    FramePreviewMesh();
}

// 섹션 선택과 단독 표시 상태를 미리보기 컴포넌트에 반영합니다.
void FObjViewer::SelectSection(int32 SectionIndex)
{
    // 선택 해제나 새 모델 로드 시 전체 표시 상태로 돌아갑니다.
    const bool bValid = PreviewMesh && SectionIndex >= 0
        && SectionIndex < PreviewMesh->GetSections().Num();
    SelectedSectionIndex = bValid ? SectionIndex : -1;
    if (!bValid) bOnlySelectedSection = false;

#if defined(OBJVIEWER_APP)
    // Viewer 전용 컴포넌트에 두 표시 상태를 함께 전달합니다.
    if (PreviewComponent)
    {
        auto* Component = static_cast<UObjViewerMeshComponent*>(PreviewComponent);
        Component->SetSectionDisplay(SelectedSectionIndex, bOnlySelectedSection);
    }
#endif
}

// 현재 시선 방향을 유지하며 모델 전체가 보이는 최소 거리로 맞춥니다.
void FObjViewer::FramePreviewMesh()
{
    if (!PreviewMesh || !EditorCamera) return;

    const D3D11_VIEWPORT Viewport = GetRenderViewport(
        GEngine::GetInstance()->GetViewport());
    float MinDistance = 0.0f, MaxDistance = 0.0f;
    if (!GetOrbitDistanceLimits(Viewport, MinDistance, MaxDistance)) return;

    // 저장된 거리와 실제 카메라 위치를 함께 변경합니다.
    OrbitDistance = MinDistance;
    EditorCamera->SetIsPerspective(true);
    EditorCamera->SetAspectRatio(Viewport.Width / Viewport.Height);
    EditorCamera->ConstrainEditorRotation();

    const FVector Forward = EditorCamera->GetForward().GetNormalized();
    EditorCamera->SetRelativeLocation(Forward * -OrbitDistance);
}

// 화면 크기와 클리핑 범위로 카메라의 최소·최대 거리를 구합니다.
bool FObjViewer::GetOrbitDistanceLimits(const D3D11_VIEWPORT& Viewport,
    float& OutMinDistance, float& OutMaxDistance) const
{
    if (!EditorCamera || Viewport.Width <= 0.0f || Viewport.Height <= 0.0f)
        return false;

    // 가로와 세로 중 좁은 시야각에 바운딩 구 전체가 들어오도록 계산합니다.
    const float Aspect = Viewport.Width / Viewport.Height;
    const float HalfVerticalFOV = EditorCamera->GetFOV() * 0.5f;
    const float HalfHorizontalFOV = std::atan(std::tan(HalfVerticalFOV) * Aspect);
    const float HalfFOV = (std::min)(HalfVerticalFOV, HalfHorizontalFOV);
    const float FramingRadius = PreviewRadius * 1.1f;

    // 화면 여백을 확보하고 가까운 면과 먼 면에 모델이 잘리지 않도록 제한합니다.
    OutMinDistance = (std::max)(FramingRadius / std::sin(HalfFOV),
        FramingRadius + EditorCamera->GetNearZ());
    OutMaxDistance = EditorCamera->GetFarZ() - FramingRadius;

    // 창이 극단적으로 좁아 두 조건을 동시에 만족하지 못하면 적용하지 않습니다.
    return std::isfinite(OutMinDistance) && std::isfinite(OutMaxDistance)
        && OutMinDistance <= OutMaxDistance;
}

// 모델을 중심으로 공전하며 전체 모델이 화면 안에 들어오도록 거리를 제한합니다.
void FObjViewer::UpdateOrbitCamera(float DeltaTime)
{
    // UI 조작 중이나 모델이 없는 동안에도 드래그 입력이 누적되지 않게 소비합니다.
    GInputManager& Input = *GInputManager::GetInstance();
    int32 DeltaX = 0, DeltaY = 0;
    Input.ConsumeRightDragDelta(DeltaX, DeltaY);
    if (!PreviewMesh || !EditorCamera) return;

    const D3D11_VIEWPORT Viewport = GetRenderViewport(
        GEngine::GetInstance()->GetViewport());
    float MinDistance = 0.0f, MaxDistance = 0.0f;
    if (!GetOrbitDistanceLimits(Viewport, MinDistance, MaxDistance)) return;

    // UI가 입력을 사용하는 동안에는 사용자 조작만 차단합니다.
    const ImGuiIO& IO = ImGui::GetIO();
    const bool bAcceptInput = !IO.WantCaptureMouse && !IO.WantCaptureKeyboard;
    if (bAcceptInput)
    {
        // 현재 시선에서 각도를 구하고 입력을 더한 뒤 Pitch를 제한합니다.
        if (Input.GetKey(GInputManager::EI_RMOUSE))
        {
            const FVector Forward = EditorCamera->GetForward().GetNormalized();
            const float RadiansPerPixel = OrbitSensitivity * PI / 180.0f;
            float Yaw = std::atan2(Forward.Y, Forward.X);
            float Pitch = std::atan2(-Forward.Z, std::hypot(Forward.X, Forward.Y));
            Yaw = std::remainder(Yaw + DeltaX * RadiansPerPixel, 2.0f * PI);
            constexpr float PitchLimit = 89.0f * PI / 180.0f;
            Pitch = std::clamp(Pitch + DeltaY * RadiansPerPixel, -PitchLimit, PitchLimit);

            const FQuaternion Rotation =
                FQuaternion::FromAxisAngle(FVector(0, 0, 1), Yaw)
                * FQuaternion::FromAxisAngle(FVector(0, 1, 0), Pitch);
            EditorCamera->SetRelativeRotation(Rotation);
        }

        // W는 접근, S는 후퇴이며 카메라의 기존 이동 속도를 재사용합니다.
        const float ZoomInput = float(Input.GetKey(GInputManager::EI_W)) - float(Input.GetKey(GInputManager::EI_S));
        OrbitDistance -= ZoomInput * EditorCamera->GetMoveSpeed() * DeltaTime;
    }

    // 입력 여부와 무관하게 창 크기 변화에 맞춰 거리와 카메라 위치를 보정합니다.
    OrbitDistance = std::clamp(OrbitDistance, MinDistance, MaxDistance);
    EditorCamera->SetIsPerspective(true);
    EditorCamera->SetAspectRatio(Viewport.Width / Viewport.Height);
    EditorCamera->ConstrainEditorRotation();

    const FVector Forward = EditorCamera->GetForward().GetNormalized();
    EditorCamera->SetRelativeLocation(Forward * -OrbitDistance);
}
