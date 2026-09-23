#pragma once

#include "EditorWindow.h"
#include <filesystem>
#include <functional>
#include <utility>

class FTextureResource;

enum class EAssetType
{
	Directory,
	StaticMesh,
	Texture,
	Unknown,
};

struct FDirectoryEntry
{
	std::filesystem::path Path;
	TArray<FDirectoryEntry> Children;
};

struct FAssetEntry
{
    std::filesystem::path Path;
    EAssetType Type;

    // 목록이 갱신되기 전까지 미리보기 요청 결과를 유지한다.
    bool bThumbnailRequested = false;
    FTextureResource* Thumbnail = nullptr; // 리소스 매니저 소유이며 여기서는 참조만 한다.
    FString ThumbnailError;
};

class UAssetBrowserWindow : public UEditorWindow
{
	UCLASS(UAssetBrowserWindow, "AssetBrowserWindow", UEditorWindow)

public:
	virtual void InitializeWindow(FEditor* InEditor, const FString& InName) override;
	virtual void Render(float DeltaTime) override;

    // 폴더 탐색은 유지하고 파일 목록을 OBJ로 제한합니다.
    void SetObjOnly(bool bInObjOnly);
    // 모델을 여는 동작은 창을 사용하는 쪽에 위임합니다.
    void SetOnObjActivated(std::function<void(const std::filesystem::path&)> Callback)
    {
        OnObjActivated = std::move(Callback);
    }

private:
    bool bObjOnly = false;
    std::function<void(const std::filesystem::path&)> OnObjActivated;
	void MoveTo(const std::filesystem::path& Path);
	
	void DrawDirectoryTreeNode(const FDirectoryEntry& Entry, bool bRoot = false);
	void DrawContentView();

	void RefreshDirectoryEntries();
	void RefreshCurrentContents();
	FDirectoryEntry ConstructDirectoryEntry(const std::filesystem::path& Path) const;
	FTextureResource* GetOrRequestThumbnail(FAssetEntry& Entry);

	std::filesystem::path RootPath = "Assets";
	std::filesystem::path CurrentPath = "Assets";
	TArray<std::filesystem::path> BackHistory;

	FDirectoryEntry RootDirectoryEntry;
	TArray<FAssetEntry> CurrentContents;

	FTextureResource* DirectoryTexture = nullptr;
	FTextureResource* StaticMeshTexture = nullptr;
	FTextureResource* FileTexture = nullptr;
};
