#include "pch.h"
#include "PlaceActorWindow.h"

#include <algorithm>
#include <cmath>

#include "Editor/Editor.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Core/Container/Array.h"
#include "Engine/Component/CameraComponent.h"
#include "Engine/Component/Primitive/FlipbookComponent.h"
#include "Engine/Component/Primitive/TextComponent.h"
#include "Engine/Component/Light/SpotLightComponent.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_stdlib.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Memory/Allocator.h"
#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Scene/SceneManager.h"
#include "Core/Util/File.h"
#include "Editor/Util/MeshSelection.h"
#include "SolarSystem.h"

void UPlaceActorWindow::SpawnStaticMesh()
{
	Editor->SpawnStaticMesh(SelectedMeshKey, NumberOfSpawn);
}

void UPlaceActorWindow::SpawnSpecialComponent()
{
	Editor->SpawnComponent(SelectedSpecialComponentClass, NumberOfSpawn);
}

void UPlaceActorWindow::SpawnEmptyActor()
{
	Editor->CreateEmptyActor();
}

void UPlaceActorWindow::InitializeWindow(FEditor* Editor, const FString& Name)
{
	UEditorWindow::InitializeWindow(Editor, Name);

	SpecialComponentClasses.Add(UTextComponent::GetClass());
	SpecialComponentClasses.Add(UFlipbookComponent::GetClass());
	SpecialComponentClasses.Add(USpotLightComponent::GetClass());

	SelectedSpecialComponentClass = *SpecialComponentClasses.begin();

	SelectedMeshKey = MeshSelection::GetDefaultKey();
}


void UPlaceActorWindow::Render(float DeltaTime)
{
	if (!bOpen)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);
	
	ImVec2 Available = ImGui::GetContentRegionAvail();
	float ButtonWidth = Available.x * 0.2f;

	ImGui::Begin(Name.c_str(), &bOpen, ImGuiWindowFlags_HorizontalScrollbar);
	{
		ImGui::Text("Spawn Number");
		if (ImGui::InputScalar(
			"###SpawnNum",
			ImGuiDataType_U32,
			&NumberOfSpawn,
			&Step))
		{
			NumberOfSpawn = std::clamp(NumberOfSpawn, 1u, 20u);
		}

		ImGui::SeparatorText("Static Mesh");

		ImGui::PushItemWidth(-1.0f);
		MeshSelection::DrawCombo("###StaticMesh",SelectedMeshKey,ImGuiComboFlags_HeightSmall);
		ImGui::PopItemWidth();

		ImGui::PushItemWidth(ButtonWidth);
		if (ImGui::Button("Spawn###StaticMeshSpawn"))
		{
			SpawnStaticMesh();
		}
		ImGui::PopItemWidth();

		ImGui::SeparatorText("Special");

		ImGui::PushItemWidth(-1.0f);
		if (ImGui::BeginCombo(
			"###Special",
			SelectedSpecialComponentClass->DisplayName.c_str(),
			ImGuiComboFlags_HeightSmall))
		{
			for (FClassType* ComponentClass : SpecialComponentClasses)
			{
				const bool bSelected = SelectedSpecialComponentClass == ComponentClass;
				if (ImGui::Selectable(ComponentClass->DisplayName.c_str(), bSelected))
				{
					SelectedSpecialComponentClass = ComponentClass;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
		if (ImGui::Button("Spawn###SpecialSpawn"))
		{
			SpawnSpecialComponent();
		}

		ImGui::Separator();
		if (ImGui::Button("Create Empty Actor"))
		{
			SpawnEmptyActor();
		}
	}

	ImGui::End();
}
