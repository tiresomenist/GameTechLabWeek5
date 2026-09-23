#include "pch.h"
#include "AssetBrowserWindow.h"

#include "Core/Util/File.h"
#include "Editor/Editor.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/TextureResource.h"
#include <exception>
#include <stdexcept>
#include <cwchar>

// 표시 모드가 바뀌면 현재 폴더의 파일 목록을 다시 구성합니다.
void UAssetBrowserWindow::SetObjOnly(bool bInObjOnly)
{
    if (bObjOnly == bInObjOnly) return;
    bObjOnly = bInObjOnly;
    RefreshCurrentContents();
}

void UAssetBrowserWindow::InitializeWindow(FEditor* InEditor, const FString& InName)
{
	UEditorWindow::InitializeWindow(InEditor, InName);

	DirectoryTexture = GResourceManager::GetInstance()->GetOrLoadTexture("Assets/Editor/Directory.png");
	StaticMeshTexture = GResourceManager::GetInstance()->GetOrLoadTexture("Assets/Editor/StaticMesh.png");
	FileTexture = GResourceManager::GetInstance()->GetOrLoadTexture("Assets/Editor/File.png");

	RefreshDirectoryEntries();
	RefreshCurrentContents();
}

void UAssetBrowserWindow::Render(float DeltaTime)
{
	if (!bOpen)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin(Name.c_str(), &bOpen);

	ImGui::BeginChild("Folder Tree", ImVec2(180.0f, 0.0f), ImGuiChildFlags_Borders);

	DrawDirectoryTreeNode(RootDirectoryEntry, true);

	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("Content View", ImVec2(0.0f, 0.0f));

	DrawContentView();

	ImGui::EndChild();

	ImGui::End();
}

void UAssetBrowserWindow::MoveTo(const std::filesystem::path& Path)
{
	const std::filesystem::path NewPath = Path.lexically_normal();

	std::error_code Error;
	if (NewPath == CurrentPath || !std::filesystem::is_directory(NewPath))
	{
		return;
	}

	BackHistory.Add(CurrentPath);
	CurrentPath = NewPath;
	RefreshCurrentContents();
}

void UAssetBrowserWindow::DrawDirectoryTreeNode(const FDirectoryEntry& Entry, bool bRoot)
{
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
	if (!Entry.Children.IsEmpty())
	{
		Flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	}
	else
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (bRoot)
	{
		Flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	FString Path = File::PathToUtf8(Entry.Path);
	FString Label = File::PathToUtf8(Entry.Path.filename());
	const bool bNodeOpen = ImGui::TreeNodeEx((Label + "###" +  Path).c_str(), Flags);
	if (ImGui::IsItemClicked())
	{
		MoveTo(Entry.Path);
	}
	if (bNodeOpen && !Entry.Children.IsEmpty())
	{
		for (const auto& Child : Entry.Children)
		{
			DrawDirectoryTreeNode(Child);
		}
		ImGui::TreePop();
	}
}

void UAssetBrowserWindow::DrawContentView()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));

	ImGui::BeginDisabled(BackHistory.IsEmpty());
	if (ImGui::ArrowButton("Back", ImGuiDir_Left))
	{
		CurrentPath = BackHistory.Pop();
		RefreshCurrentContents();
	}
	ImGui::EndDisabled();
	
	ImGui::SameLine();
	ImGui::BeginDisabled(CurrentPath.lexically_normal() == RootPath.lexically_normal());
	if (ImGui::ArrowButton("Up", ImGuiDir_Up))
	{
		MoveTo(CurrentPath.parent_path());
	}
	ImGui::EndDisabled();

	ImGui::PopStyleVar();

	ImGui::SameLine();
	if (ImGui::SmallButton("Refresh"))
	{
		RefreshDirectoryEntries();
		RefreshCurrentContents();
	}

	const std::filesystem::path Relative = RootPath.filename() / CurrentPath.lexically_relative(RootPath);
	ImGui::SameLine();
	ImGui::SeparatorText(File::PathToUtf8(Relative).c_str());

	constexpr float ThumbnailSize = 64.0f;
	constexpr float CellSize = ThumbnailSize + 20.0f;

	const float AvailableRegion = ImGui::GetContentRegionAvail().x;
	const int ColumnCount = std::max(1, static_cast<int>(AvailableRegion / CellSize));

	std::filesystem::path NextPath;
	if (ImGui::BeginTable("AssetGrid", ColumnCount, ImGuiTableFlags_SizingStretchSame))
	{
		for (auto& Entry : CurrentContents)
		{
			ImGui::TableNextColumn();

			const FString Path = File::PathToUtf8(Entry.Path);
			const FString Label = File::PathToUtf8(Entry.Path.filename());

			ImGui::PushID(Path.c_str());

			const float CellWidth = ImGui::GetContentRegionAvail().x;
			const float Size = std::min(CellWidth, ThumbnailSize);

			if (Entry.Type == EAssetType::StaticMesh)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::ImageButton("StaticMesh", ImTextureRef(StaticMeshTexture->GetSRV()), ImVec2(Size, Size));
				ImGui::PopStyleColor();
				// Viewer는 여기서 로딩하지 않고 다음 Tick에 처리할 경로만 받습니다.
				if (OnObjActivated && ImGui::IsItemHovered()
					&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					OnObjActivated(Entry.Path);
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("STATIC_MESH", Path.c_str(), Path.size() + 1);
					ImGui::Image(ImTextureRef(StaticMeshTexture->GetSRV()), ImVec2(Size, Size));
					ImGui::EndDragDropSource();
				}
			}
			else if (Entry.Type == EAssetType::Texture)
			{
				FTextureResource* Texture = GetOrRequestThumbnail(Entry);
				if (Texture) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
					ImGui::ImageButton("Thumbnail", ImTextureRef(Texture->GetSRV()), ImVec2(Size, Size));
					ImGui::PopStyleColor();
					if (ImGui::BeginDragDropSource())
					{
						ImGui::SetDragDropPayload("TEXTURE", Path.c_str(), Path.size() + 1);
						ImGui::Image(ImTextureRef(Texture->GetSRV()), ImVec2(Size, Size));
						ImGui::EndDragDropSource();
					}
				}
				else {
					ImGui::Button("Failed##Thumbnail", ImVec2(Size, Size));
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(Entry.ThumbnailError.c_str());
						ImGui::Separator();
						ImGui::TextUnformatted("Fix the file, then click Refresh to retry.");
						ImGui::EndTooltip();
					}
				}
			}
			else if (Entry.Type == EAssetType::Directory)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::ImageButton("Directory", ImTextureRef(DirectoryTexture->GetSRV()), ImVec2(Size, Size));
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::BeginDisabled(true);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::ImageButton("File", ImTextureRef(FileTexture->GetSRV()), ImVec2(Size, Size));
				ImGui::PopStyleColor();
				ImGui::EndDisabled();
			}

			const bool bHovered = ImGui::IsItemHovered();
			if (Entry.Type == EAssetType::Directory && 
				bHovered && 
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				NextPath = Entry.Path;
			}

			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + CellWidth);
			ImGui::TextUnformatted(Label.c_str());
			ImGui::PopTextWrapPos();

			ImGui::Spacing();

			ImGui::PopID();
		}
		
		ImGui::EndTable();
	}

	if (!NextPath.empty())
	{
		MoveTo(NextPath);
	}
}

void UAssetBrowserWindow::RefreshDirectoryEntries()
{
	if (RootPath.empty() ||
		!std::filesystem::exists(RootPath) ||
		!std::filesystem::is_directory(RootPath))
	{
		RootDirectoryEntry = {};
		return;
	}

	RootDirectoryEntry = ConstructDirectoryEntry(RootPath);
}

void UAssetBrowserWindow::RefreshCurrentContents()
{
	CurrentContents.Empty();

	if (CurrentPath.empty() ||
		!std::filesystem::exists(CurrentPath) ||
		!std::filesystem::is_directory(CurrentPath))
	{
		return;
	}

	TArray<FAssetEntry> DirectoryEntries;
	TArray<FAssetEntry> FileEntries;
	for (const auto& Entry : std::filesystem::directory_iterator(CurrentPath))
	{
		FAssetEntry AssetEntry{
			.Path = Entry.path(),
			.Type = Entry.is_directory() ? EAssetType::Directory : EAssetType::Unknown,
		};
		if (Entry.is_regular_file())
		{
			// Viewer에서만 확장자 대소문자에 관계없이 OBJ 파일을 표시합니다.
			if (bObjOnly)
			{
				if (_wcsicmp(Entry.path().extension().c_str(), L".obj") != 0) continue;
				AssetEntry.Type = EAssetType::StaticMesh;
				FileEntries.Add(AssetEntry);
				continue;
			}
			const FString Extension = File::PathToUtf8(Entry.path().extension());
			if (Extension == ".obj")
			{
				AssetEntry.Type = EAssetType::StaticMesh;
			}
			else if (Extension == ".meshcache")
			{
				continue;
			}
			else if (Extension == ".png")
			{
				AssetEntry.Type = EAssetType::Texture;
			}
		}

		if (AssetEntry.Type == EAssetType::Directory)
		{
			DirectoryEntries.Add(AssetEntry);
		}
		else
		{
			if (bObjOnly) continue;
			FileEntries.Add(AssetEntry);
		}
	}

	for (const auto& Entry : DirectoryEntries)
	{
		CurrentContents.Add(Entry);
	}
	for (const auto& Entry : FileEntries)
	{
		CurrentContents.Add(Entry);
	}
}

FDirectoryEntry UAssetBrowserWindow::ConstructDirectoryEntry(const std::filesystem::path& Path) const
{
	FDirectoryEntry Result{
		.Path = Path,
	};
	if (!std::filesystem::is_directory(Path))
	{
		return Result;
	}
	for (const auto& Entry : std::filesystem::directory_iterator(Path))
	{
		if (Entry.is_directory())
		{
			Result.Children.Add(ConstructDirectoryEntry(Entry.path()));
		}
	}
	return Result;
}

// 텍스처 미리보기 로딩 실패를 저장하여 매 프레임 재시도하지 않는다.
FTextureResource* UAssetBrowserWindow::GetOrRequestThumbnail(FAssetEntry& Entry)
{
	// 성공과 실패 모두 이미 요청한 결과를 재사용한다.
	if (Entry.bThumbnailRequested) return Entry.Thumbnail;
	Entry.bThumbnailRequested = true;

	try
	{
		// 실제 텍스처 로딩과 소유권 관리는 기존 리소스 매니저에 맡긴다.
		FTextureResource* Texture = GResourceManager::GetInstance()->GetOrLoadTexture(
			File::PathToUtf8(Entry.Path));
		if (!Texture || !Texture->GetSRV())
		{
			throw std::runtime_error("Texture preview is unavailable.");
		}
		Entry.Thumbnail = Texture;
	}
	catch (const std::exception& Error)
	{
		// 실패한 항목은 빈 미리보기와 오류 설명을 유지한다.
		Entry.ThumbnailError = Error.what();
	}
	return Entry.Thumbnail;
}
