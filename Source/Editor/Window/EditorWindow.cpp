#include "pch.h"
#include "EditorWindow.h"

void UEditorWindow::InitializeWindow(FEditor* InEditor, const FString& InName)
{
	Editor = InEditor;
	Name = InName;
}

void UEditorWindow::DrawItemBottomLine(uint32 Color, float Thickness)
{
	ImVec2 Min = ImGui::GetItemRectMin();
	ImVec2 Max = ImGui::GetItemRectMax();

	ImGui::GetWindowDrawList()->AddLine(
		ImVec2(Min.x, Max.y),
		ImVec2(Max.x, Max.y),
		Color,
		Thickness
	);
}
