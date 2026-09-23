#include "pch.h"
#include "EditorMenuLayout.h"

#include "Core/Util/File.h"
#include "Editor/Editor.h"
#include "Editor/Window/EditorWindow.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_stdlib.h"

void FEditorMenuLayout::Initialize(FEditor* InEditor)
{
	Editor = InEditor;
}

void FEditorMenuLayout::Draw()
{
	ImGui::BeginMainMenuBar();

	static bool bRequestedLoad = false;
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene"))
		{
			NewScene();
		}

		if (ImGui::MenuItem("Save Scene"))
		{
			SaveScene();
		}

		if (ImGui::MenuItem("Load Scene"))
		{
			bRequestedLoad = true;
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		for (UEditorWindow* Window : Editor->GetWindows())
		{
			if (ImGui::MenuItem(Window->GetWindowName().c_str(), nullptr, Window->GetOpenPtr()))
			{
				Window->OpenWindow();
			}
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();

	BuildDefaultLayout(false);
	ImGui::DockSpaceOverViewport(GetDockSpaceID(), ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

	const ImGuiDockNode* ViewportNode = ImGui::DockBuilderGetCentralNode(GetDockSpaceID());
	if (ViewportNode)
	{
		const ImVec2 Origin = ImGui::GetMainViewport()->Pos;
		Editor->OnResize(
			ViewportNode->Size.x,
			ViewportNode->Size.y,
			ViewportNode->Pos.x - Origin.x,
			ViewportNode->Pos.y - Origin.y);
	}

	if (bRequestedLoad)
	{
		LoadScene();
		bRequestedLoad = false;
	}

	Editor->DrawStatOverlay();
}

void FEditorMenuLayout::NewScene()
{
	Editor->NewScene();
}

void FEditorMenuLayout::SaveScene()
{
	// imgui_impl_win32가 메인 뷰포트에 HWND를 넣어두므로 그걸 대화상자 owner로 사용
	const HWND Owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);

	const std::optional<std::filesystem::path> ScenePath = File::SaveFileDialog(Owner, EFileDialogType::Json, "Scenes");
	if (!ScenePath)
	{
		return; // 취소
	}

	Editor->SaveSceneToPath(ScenePath.value());
}

void FEditorMenuLayout::LoadScene()
{
	// imgui_impl_win32가 메인 뷰포트에 HWND를 넣어두므로 그걸 대화상자 owner로 사용
	const HWND Owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);

	const std::optional<std::filesystem::path> ScenePath = File::OpenFileDialog(Owner, EFileDialogType::Json, "Scenes");
	if (!ScenePath)
	{
		return; // 취소
	}

	Editor->LoadSceneFromPath(ScenePath.value());
}

void FEditorMenuLayout::BuildDefaultLayout(bool bReset)
{
	static bool bFirstTime = true;
	if (!bReset && !bFirstTime)
	{
		return;
	}

	const ImGuiID DockSpaceID = GetDockSpaceID();

	ImGui::DockBuilderRemoveNode(DockSpaceID);
	ImGui::DockBuilderAddNode(DockSpaceID, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::DockBuilderSetNodeSize(DockSpaceID, ImGui::GetMainViewport()->WorkSize);

	ImGuiID MainID = DockSpaceID;

	ImGuiID RightID = ImGui::DockBuilderSplitNode(MainID, ImGuiDir_Right, 0.2f, nullptr, &MainID);
	const ImGuiID PropertyID = ImGui::DockBuilderSplitNode(RightID, ImGuiDir_Down, 0.7f, nullptr, &RightID);
	const ImGuiID OutlinerID = RightID;
	const ImGuiID ConsoleID = ImGui::DockBuilderSplitNode(MainID, ImGuiDir_Down, 0.2f, nullptr, &MainID);
	const ImGuiID SceneID = ImGui::DockBuilderSplitNode(MainID, ImGuiDir_Left, 0.2f, nullptr, &MainID);
	const ImGuiID ViewportToolbarID = ImGui::DockBuilderSplitNode(MainID, ImGuiDir_Up, 0.05f, nullptr, &MainID);

	if (ImGuiDockNode* OutlinerNode = ImGui::DockBuilderGetNode(OutlinerID))
	{
		OutlinerNode->LocalFlags |= ImGuiDockNodeFlags_NoCloseButton;
		OutlinerNode->UpdateMergedFlags();
	}
	ImGui::DockBuilderDockWindow("Outliner", OutlinerID);

	if (ImGuiDockNode* PropertyNode = ImGui::DockBuilderGetNode(PropertyID))
	{
		PropertyNode->LocalFlags |= ImGuiDockNodeFlags_NoCloseButton;
		PropertyNode->UpdateMergedFlags();
	}
	ImGui::DockBuilderDockWindow("Properties", PropertyID);

	if (ImGuiDockNode* ConsoleNode = ImGui::DockBuilderGetNode(ConsoleID))
	{
		ConsoleNode->LocalFlags |= ImGuiDockNodeFlags_NoCloseButton;
		ConsoleNode->UpdateMergedFlags();
	}
	ImGui::DockBuilderDockWindow("Asset Browser", ConsoleID);
	ImGui::DockBuilderDockWindow("Console", ConsoleID);
	ImGui::DockBuilderDockWindow("Debug", ConsoleID);

	if (ImGuiDockNode* SceneNode = ImGui::DockBuilderGetNode(SceneID))
	{
		SceneNode->LocalFlags |= ImGuiDockNodeFlags_NoCloseButton;
		SceneNode->UpdateMergedFlags();
	}
	ImGui::DockBuilderDockWindow("Place Actors", SceneID);

	if (ImGuiDockNode* ToolbarNode =
		ImGui::DockBuilderGetNode(ViewportToolbarID))
	{
		ToolbarNode->LocalFlags |=
			ImGuiDockNodeFlags_NoResize |
			ImGuiDockNodeFlags_NoUndocking |
			ImGuiDockNodeFlags_NoDockingSplit |
			ImGuiDockNodeFlags_NoDockingOverMe |
			ImGuiDockNodeFlags_NoTabBar |
			ImGuiDockNodeFlags_NoWindowMenuButton |
			ImGuiDockNodeFlags_NoCloseButton;

		ToolbarNode->UpdateMergedFlags();
	}
	ImGui::DockBuilderDockWindow("Viewport Toolbar", ViewportToolbarID);

	ImGui::DockBuilderFinish(DockSpaceID);
	bFirstTime = false;
}
