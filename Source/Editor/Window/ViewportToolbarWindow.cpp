#include "pch.h"
#include "ViewportToolbarWindow.h"

#include "Editor/Editor.h"

void UViewportToolbarWindow::Render(float DeltaTime)
{
    TArray<FViewportClient>& Viewports = Editor->GetViewports();

    constexpr ImGuiWindowFlags Flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoDocking;
    
    //ImGui::SetNextWindowPos(ImVec2(VP.TopLeftX, VP.TopLeftY), ImGuiCond_Always);

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Stats"))
        {
            if (ImGui::MenuItem("Hide Stat"))
            {
                Editor->HideAllStats();
            }
            if (ImGui::MenuItem("Show Stat"))
            {
                Editor->ShowAllStats();
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }

    for (uint32 i = 0; i < Viewports.Num(); ++i)
    {
        FViewportClient& VC = Viewports[i];
        const D3D11_VIEWPORT& VP = VC.GetViewportInfo();

        ImGui::SetNextWindowPos(ImVec2(VP.TopLeftX, VP.TopLeftY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(VP.Width, 40.0f), ImGuiCond_Always);
        
        FString name = std::format("{}{}", Name.c_str(), i);
        bool IsMaximize = (Editor->GetCurrMaxViewIdx() == i);

        if (-1 != Editor->GetCurrMaxViewIdx() && !IsMaximize)
        {
            continue;
        }

        ImGui::Begin(name.c_str(), nullptr, Flags);
        {
            UCameraComponent* Camera = VC.GetCamera();

   	        const char* ProjectionLabel = Camera->GetIsPerspective() ? "Perspective" : "Orthogonal";
            //ImGui::SetNextWindowPos(ImVec2(VP.TopLeftX, VP.TopLeftY), ImGuiCond_Always);
               BeginPopupButton(ProjectionLabel, "ProjectionPopup", [this, Camera, &VC, VP]()
                   {
                      DrawProjectionPopup(Camera, VC);
                   });
               ImGui::SameLine();
       
               const char* ViewModeLabel = "Unknown";
               for (const FViewModeEntry& Entry : ViewModeEntries)
               {
                   if (Entry.Mode == VC.GetViewMode())
                   {
   			            ViewModeLabel = Entry.Name;
                   }
               }
               BeginPopupButton(ViewModeLabel, "ViewModePopup", [this, &VC]()
                   {
                       DrawViewModePopup(VC);
                   });
               ImGui::SameLine();
               
               BeginPopupButton("Show Flags", "ShowFlagsPopup", [this, &VC]()
                   {
                       DrawShowFlagsPopup(VC);
                   });
               ImGui::SameLine();
               const char* WindowButton = IsMaximize ? "Restore" : "Maximize";
               if (ImGui::Button(WindowButton))
               {
                   Editor->ToggleMaxView(i);
               }
        }
        ImGui::End();
	}
   

}

void UViewportToolbarWindow::BeginPopupButton(const char* ButtonName, const char* PopupName,
	const std::function<void()>& DrawFunction)
{
    if (ImGui::Button(ButtonName))
    {
        ImGui::OpenPopup(PopupName);
    }

    const ImVec2 ButtonMin = ImGui::GetItemRectMin();
    const ImVec2 ButtonMax = ImGui::GetItemRectMax();
    ImGui::SetNextWindowPos(ImVec2(ButtonMin.x, ButtonMax.y), ImGuiCond_Appearing);

    if (ImGui::BeginPopup(PopupName))
    {
        DrawFunction();
        ImGui::EndPopup();
    }
}

void UViewportToolbarWindow::DrawProjectionPopup(UCameraComponent* Camera, FViewportClient& InVC)
{
    int ProjectionSelection = Camera->GetIsPerspective() ? 0 : 1;

    // if (지금 카메라가 perspective 카메라면) 아래로직 실행. 직교투영(탑, 프론트, 오른쪽)일때는 아예 버튼 없애기
    //uint32 currViewIdx = Editor->GetCurrentEditViewportIndex();
    //const TArray<FViewportClient>& Viewports = Editor->GetViewports();
    if (InVC.GetViewportType() != EViewportType::Perspective)
    {
        if (ImGui::RadioButton("Orthogonal", true))
        {
            Camera->SetIsPerspective(false);
        }
    }
    else
    {
	    const char* ProjectionNames[] = { "Perspective", "Orthogonal" };
    	for (int i = 0; i < 2; ++i)
    	{
    		if (ImGui::RadioButton(ProjectionNames[i], ProjectionSelection == i))
    		{
    			ProjectionSelection = i;
    			ImGui::CloseCurrentPopup();
    		}
    	}
    	Camera->SetIsPerspective(ProjectionSelection == 0);
    }

    // TODO: Orthographic 방향에 따라서 옵션 추가 분리

    ImGui::Separator();

    ImGui::PushItemWidth(100.0f);

    float CameraMoveSpeed = Camera->GetMoveSpeed();
    if (ImGui::SliderFloat("Move Speed", &CameraMoveSpeed, 10.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    {
        Camera->SetMoveSpeed(std::clamp(CameraMoveSpeed, 0.1f, 100.0f));
    }

    if (InVC.GetViewportType() == EViewportType::Perspective)
    {
        float FOV = Camera->GetFOV() * 180.0f / PI;
        if (ImGui::SliderFloat("Field of View", &FOV, 5.0f, 170.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        {
            Camera->SetFOVByDegree(FOV);
        }
    }

    ImGui::PopItemWidth();

    ImGui::Separator();

    FVector CameraLocation = InVC.GetCamera()->GetRelativeLocation();
    static bool bEditingCameraRotation = false;
    static FVector CameraRotationDegree;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.2f); // Item 너비 설정
    ImGui::DragFloat("##cameraX", &CameraLocation.X, 0.1f);
    DrawItemBottomLine(IM_COL32(210, 15, 57, 255), 2.0f);
    ImGui::SameLine();
    ImGui::DragFloat("##cameraY", &CameraLocation.Y, 0.1f);
    DrawItemBottomLine(IM_COL32(64, 160, 43, 255), 2.0f);
    ImGui::SameLine();
    ImGui::DragFloat("##cameraZ", &CameraLocation.Z, 0.1f);
    DrawItemBottomLine(IM_COL32(30, 102, 245, 255), 2.0f);
    ImGui::SameLine();
    ImGui::Text("Camera Location");
    bool bRotationChanged = false;
    bool bRotationActive = false;
    bool bRotationFinished = false;
    constexpr ImGuiSliderFlags PitchFlags = ImGuiSliderFlags_AlwaysClamp;
    constexpr ImGuiSliderFlags YawFlags = ImGuiSliderFlags_WrapAround | ImGuiSliderFlags_AlwaysClamp;

    if (InVC.GetViewportType() == EViewportType::Perspective)
    {
        // ConstrainEditorRotation()이 Roll을 제거하므로 수정할 수 없는 값으로 표시한다.
        if (!bEditingCameraRotation)
        {
            const FRotator& CameraRotation = Camera->GetRelativeRotator();
            CameraRotationDegree = CameraRotation.ToEulerDegrees();
        }
        ImGui::BeginDisabled();
        ImGui::DragFloat("##cameraRX", &CameraRotationDegree.X, 0.1f, -180.0f, 180.0f, "%.3f");
        ImGui::EndDisabled();
        bRotationActive |= ImGui::IsItemActive();
        bRotationFinished |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        bRotationChanged |= ImGui::DragFloat("##cameraRY", &CameraRotationDegree.Y, 0.1f, -89.9f, 89.9f, "%.3f", PitchFlags);
        bRotationActive |= ImGui::IsItemActive();
        bRotationFinished |= ImGui::IsItemDeactivatedAfterEdit();
        DrawItemBottomLine(IM_COL32(64, 160, 43, 255), 2.0f);
        ImGui::SameLine();
        bRotationChanged |= ImGui::DragFloat("##cameraRZ", &CameraRotationDegree.Z, 0.1f, 0.0f, 0.0f, "%.3f", YawFlags);
        bRotationActive |= ImGui::IsItemActive();
        bRotationFinished |= ImGui::IsItemDeactivatedAfterEdit();
        DrawItemBottomLine(IM_COL32(30, 102, 245, 255), 2.0f);
        ImGui::SameLine();
        ImGui::Text("Camera Rotation");
        if (bRotationChanged || bRotationFinished)
        {
            CameraRotationDegree.X = 0.0f;

            // 카메라 Pitch를 도 단위로 제한함
            CameraRotationDegree.Y = std::clamp(CameraRotationDegree.Y, -89.9f, 89.9f);

            // 도 단위 입력값을 FRotator Setter로 전달함
            Camera->SetRelativeRotation(FRotator::FromEulerDegrees(CameraRotationDegree));
        }
        bEditingCameraRotation = bRotationActive;
        ImGui::PopItemWidth();
        InVC.GetCamera()->SetRelativeLocation(CameraLocation);
    }

    ImGui::Separator();

    ImGui::PushItemWidth(100.0f);
    float GridInterval = Editor->GetGrid().Interval;
    if (ImGui::SliderFloat("Grid Spacing", &GridInterval, FGrid::MinInterval, FGrid::MaxInterval, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    {
        Editor->SetGridInterval(GridInterval);
    }

    ImGui::PopItemWidth();
}

void UViewportToolbarWindow::DrawViewModePopup(FViewportClient& InVC)
{
    for (const FViewModeEntry& Entry : ViewModeEntries)
    {
        if (ImGui::RadioButton(Entry.Name, Entry.Mode == InVC.GetViewMode()))
        {
            InVC.SetViewMode(Entry.Mode);
            ImGui::CloseCurrentPopup();
        }
    }
}

void UViewportToolbarWindow::DrawShowFlagsPopup(FViewportClient& InVC)
{
    bool bShowUUIDLabels = InVC.IsShowingUUIDLabels();
    if (ImGui::Checkbox("UUID", &bShowUUIDLabels))
    {
        InVC.SetShowUUIDLabels(bShowUUIDLabels);
    }
    bool bShowBoundingBoxes = InVC.IsShowingBoundingBoxes();
    if (ImGui::Checkbox("Bounding Boxes", &bShowBoundingBoxes))
    {
        InVC.SetShowBoundingBoxes(bShowBoundingBoxes);
    }
    bool bShowPrimitives = InVC.IsShowingPrimitives();
    if (ImGui::Checkbox("Primitives", &bShowPrimitives))
    {
        InVC.SetShowPrimitives(bShowPrimitives);
    }
    bool bShowGrid = InVC.IsShowingGrid();
    if (ImGui::Checkbox("Grid", &bShowGrid))
    {
        InVC.SetShowGrid(bShowGrid);
    }
    bool bShowWorldAxis = InVC.IsShowingWorldAxis();
    if (ImGui::Checkbox("World Axis", &bShowWorldAxis))
    {
        InVC.SetShowWorldAxis(bShowWorldAxis);
    }
}
