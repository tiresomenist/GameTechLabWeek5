#pragma once

#include "Core/Container/String.h"
#include "Engine/Resource/MeshNames.h"
#include "Engine/Object/ObjectIterator.h"
#include "ImGui/imgui.h"

namespace MeshSelection
{
    inline FName GetDefaultKey()
    {
        return GetMeshNames().Sphere;
    }

    inline bool DrawCombo(const char* Label, FName& SelectedKey, ImGuiComboFlags Flags = 0)
    {
        const char* Preview = nullptr;
        // 목록에 없는 메시도 현재 이름을 표시함

        FString FallbackLabel;
        std::filesystem::path SelectedPath = File::PathFromUtf8(SelectedKey.ToString());
        if (std::filesystem::exists(SelectedPath))
        {
			FallbackLabel = File::PathToUtf8(SelectedPath.stem().filename());
        }
        else 
        {
            FallbackLabel = SelectedKey.ToString();
        }
        Preview = FallbackLabel.c_str();

        bool bChanged = false;

        if (ImGui::BeginCombo(Label, Preview, Flags))
        {
            for (const auto& Mesh : TObjectRange<UStaticMesh>())
            {
                const FString MeshName = Mesh->GetMeshKey().ToString();
				const char* MeshPath = MeshName.c_str();
                const std::filesystem::path MeshFilePath = File::PathFromUtf8(MeshPath);
				const FString DisplayName = File::PathToUtf8(MeshFilePath.stem().filename());

                ImGui::PushID(MeshName.c_str());

				const bool bSelected = SelectedKey == Mesh->GetMeshKey();
				if (ImGui::Selectable(DisplayName.c_str(), bSelected))
				{
					if (!bSelected)
					{
						SelectedKey = Mesh->GetMeshKey();
						bChanged = true;
					}
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        return bChanged;
    }
}