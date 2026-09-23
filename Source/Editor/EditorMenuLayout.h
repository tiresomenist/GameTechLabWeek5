#pragma once

#include "Core/Container/String.h"
#include "ImGui/imgui_internal.h"

class FEditor;

class FEditorMenuLayout
{
public:
	void Initialize(FEditor* InEditor);
	void Draw();

private:
	FEditor* Editor = nullptr;
	static ImGuiID GetDockSpaceID() { return ImHashStr("EditorDockSpace"); }

	void NewScene();
	void SaveScene();
	void LoadScene();

	void BuildDefaultLayout(bool bReset = false);
};
