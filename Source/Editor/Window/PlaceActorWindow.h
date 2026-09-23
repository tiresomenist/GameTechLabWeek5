#pragma once

#include "EditorWindow.h"
#include "Engine/Object/ClassType.h"
#include "Core/Math/Matrix.h"
#include "Core/Container/String.h"
#include "Core/Math/Quaternion.h"
#include "Core/Name/Name.h"
class FEditor;

class UPlaceActorWindow : public UEditorWindow
{
    UCLASS(UPlaceActorWindow, "SceneWindow", UEditorWindow)

private:
	uint32 NumberOfSpawn = 1;
	uint32 Step = 1;
	FName SelectedMeshKey;

	TArray<FClassType*> SpecialComponentClasses;
	FClassType* SelectedSpecialComponentClass = nullptr;

public:
	void SpawnStaticMesh();
	void SpawnSpecialComponent();
	void SpawnEmptyActor();

	virtual void InitializeWindow(FEditor* InEditor, const FString& Name) override;
	void Render(float DeltaTime) override;
};
