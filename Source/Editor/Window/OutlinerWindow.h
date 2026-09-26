#pragma once

#include "Editor/Window/EditorWindow.h"
#include "Core/Container/Map.h"

class AActor;
class USceneComponent;
class UScene;

struct FOutlinerRow
{
	AActor* Actor;
	int32 Depth;
};


class UOutlinerWindow : public UEditorWindow
{
	UCLASS(UOutlinerWindow, "OutlinerWindow", UEditorWindow)

public:
	virtual void Render(float DeltaTime) override;
	void FinishRename(bool bApply);
	void InvalidateRows(){ bRowsDirty = true; }
	void OnActorDeleting(AActor* Actor);

private:
	//void DrawActorTree(AActor* Actor);
	void SetVisibilitySubtree(AActor* Actor, bool bVisible);
	void DrawRenameInput(AActor* Actor, const ImVec2& Position, float Width);

	void RequestRename(AActor* Actor);

	bool CanReparent(AActor* Source, AActor* Target) const;

	AActor* RenameTarget = nullptr;
	FString RenameBuffer;
	bool bFocusRenameInput = false;
	bool bRenameInputDrawn = false;

	AActor* PendingDeleteTarget = nullptr;
	
	AActor* PendingReparentSource = nullptr;
	AActor* PendingReparentTarget = nullptr;

	TArray<FOutlinerRow> AllRows;
	TMap<uint32, bool> CollapsedActors;
	TMap<uint32, FString> CachedLabel;

	bool bRowsDirty = true;

	void RebuildRows(UScene* Scene);
	void AppendRows(AActor* Actor, int32 Depth);
	void DrawActorRow(const FOutlinerRow& Row);
	const FString& GetCachedName(AActor* Actor);

	UScene* CachedScene = nullptr;

	AActor* PopupActor = nullptr;

	void ResetSceneCache();
};
