#pragma once

#include "EditorWindow.h"

class UDebugWindow : public UEditorWindow
{
	UCLASS(UDebugWindow, "DebugWindow", UEditorWindow)

public:
	void Render(float DeltaTime) override;
};
