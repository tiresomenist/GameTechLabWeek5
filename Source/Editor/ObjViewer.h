#pragma once
#include "Editor/Editor.h"
#include <filesystem>

class UScene;
class AActor;
class UStaticMesh;
class UStaticMeshComponent;

class FObjViewer : public FEditor
{
public:
    // Viewer 전용 씬, 카메라, 표시 요소를 생성합니다.
    void Initialize() override;

    // Viewer 씬과 카메라 입력을 갱신합니다.
    void Tick(float DeltaTime) override;

    // 공통 표시 요소와 Viewer 전용 씬을 정리합니다.
    void Release() override;

    // Viewer가 소유한 미리보기 씬을 반환합니다.
    UScene* GetCurrentScene() override;

    // Viewer의 메뉴를 구성합니다.
    void DrawMenu() override;

    // Viewer의 도구 창을 구성합니다.
    void DrawWindows(float DeltaTime) override;

    // 오브젝트 변형용 기즈모의 표시 여부를 반환합니다.
    bool ShouldDrawEditorGizmos() const override;

    // 단일 카메라로 전체 출력 영역을 사용하는 뷰를 구성합니다.
    TArray<FRenderView> BuildRenderViews(const D3D11_VIEWPORT& FullViewport) const override;

    D3D11_VIEWPORT GetRenderViewport(const D3D11_VIEWPORT& FullViewport) const override;

private:
    uint32 ViewerDockSpaceId = 0;
    // 기본 도킹 배치를 만들고 중앙 모델 출력 영역을 확보합니다.
    void DrawDockLayout();

    float MenuBarHeight = 0.0f;
    UScene* PreviewScene = nullptr;

    //Obj 파일 관련
    void OpenObjDialog();

    bool bOpenObjDialogRequested = false;
    std::filesystem::path PendingObjPath;
    // 공통 검증과 오류 처리를 거쳐 미리보기 모델을 엽니다.
    void OpenObjPath(const std::filesystem::path& FilePath);
    std::filesystem::path SelectedObjPath;
    FString SelectedObjName;
    FString FileSelectionError;

    // 기존 메시 로더로 OBJ를 읽고 미리보기 모델을 교체합니다.
    void LoadPreviewMesh(const std::filesystem::path& FilePath);

    // PreviewScene이 소유한 현재 모델 Actor를 참조합니다.
    AActor* PreviewActor = nullptr;

    //선택 된 섹션
    UStaticMesh* PreviewMesh = nullptr;
    int32 SelectedSectionIndex = -1;
    bool bOnlySelectedSection = false;

    // PreviewActor가 소유한 메시 컴포넌트를 참조합니다.
    UStaticMeshComponent* PreviewComponent = nullptr;

    // 목록 선택과 화면 강조에 사용할 섹션 번호를 함께 변경합니다.
    void SelectSection(int32 SectionIndex);

    // 모델 정규화와 카메라 거리 계산에서 같은 반지름을 사용합니다.
    static constexpr float PreviewRadius = 5.0f;

    // 현재 시선 방향을 유지하면서 모델 전체를 화면에 맞춥니다.
    void FramePreviewMesh();

    // 모델 중심에서 카메라까지의 거리를 유지합니다.
    float OrbitDistance = 25.0f;

    // 마우스 한 픽셀 이동에 대응하는 회전 각도입니다.
    static constexpr float OrbitSensitivity = 0.5f;

    // 현재 출력 영역에서 모델 전체가 보이는 거리 범위를 계산합니다.
    bool GetOrbitDistanceLimits(const D3D11_VIEWPORT& Viewport,
        float& OutMinDistance, float& OutMaxDistance) const;

    // 입력으로 회전·거리를 변경하고 모델 중심을 기준으로 카메라를 배치합니다.
    void UpdateOrbitCamera(float DeltaTime);
};
