#pragma once

#include "EditorWindow.h"
#include "Core/Container/String.h"
#include "Core/Container/Deque.h"
#include "Engine/Console.h"
#include "Core/Core.h"
#include "ImGui/imgui.h"
#include "Core/Math/Matrix.h"

class UConsoleWindow : public UEditorWindow
{
    UCLASS(UConsoleWindow, "ConsoleWindow", UEditorWindow)

private:
	ImGuiTextFilter Filter;
	TDeque<FString> logs;
public:
	void AddDebugText(FString DebugText);
	void AddDebugError(FString ErrorText);
	void Clear();
	void Copy();
	void Option();

	void Render(float DeltaTime) override;
};

