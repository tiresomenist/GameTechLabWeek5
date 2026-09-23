#pragma once

#include "Engine/Engine.h"
#include "Engine/Object/Object.h"
#include "Imgui/imgui.h"
#include <Windows.h>

class FEditor;

// UObject::Initialize()가 필요한 경우 구현하여 사용
class UEditorWindow : public UObject
{
    UCLASS(UEditorWindow, "EditorWindow", UObject)

protected:

	FEditor* Editor = nullptr;
	FString Name;
	
	bool bOpen = true;

public:
	virtual void InitializeWindow(FEditor* InEditor, const FString& InName);
	virtual void Render(float DeltaTime) {}
	void DrawItemBottomLine(uint32 Color, float Thickness);
	const FString& GetWindowName() const { return Name; }
	void OpenWindow() { bOpen = true; }
	bool* GetOpenPtr() { return &bOpen; }
};

