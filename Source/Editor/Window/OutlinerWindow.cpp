#include "pch.h"
#include "OutlinerWindow.h"

#include "Editor/Editor.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Component/ActorComponent.h"
#include "Engine/Component/SceneComponent.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "ImGui/imgui_stdlib.h"

void UOutlinerWindow::Render(float DeltaTime)
{
	bRenameInputDrawn = false;

	UScene* Scene = Editor->GetCurrentScene();
	if (CachedScene != Scene)
	{
		ResetSceneCache();
		CachedScene = Scene;
	}
	
	if (!bOpen)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin(Name.c_str(), &bOpen)) {
		ImGui::End();
		return;
	}

	if (Scene == nullptr)
	{
		ImGui::End();
		return;
	}

	const bool bCanUseShortcuts =
		RenameTarget == nullptr &&
		ImGui::IsWindowFocused() &&
		!ImGui::IsAnyItemActive() &&
		!ImGui::GetIO().WantTextInput &&
		!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup | ImGuiPopupFlags_AnyPopupLevel);

	if (bCanUseShortcuts)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
		{
			RequestRename(Editor->GetSelectedActor());
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PendingDeleteTarget = Editor->GetSelectedActor();
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::InvisibleButton("##DropTop", ImVec2(-1.0f, 4.0f));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("EDITOR_ACTOR"))
		{
			AActor* SourceActor = *static_cast<AActor**>(Payload->Data);
			if (CanReparent(SourceActor, nullptr))
			{
				PendingReparentSource = SourceActor;
				PendingReparentTarget = nullptr;
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::PopStyleVar();

	if (ImGui::BeginTable("ActorTable", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("##Visible", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, ImGui::GetFrameHeight());

		if (bRowsDirty) { RebuildRows(Scene); }

		ImGuiListClipper Clipper;
		Clipper.Begin(AllRows.Num());
		
		auto IncludeActor = [&](AActor* Actor, FInteractionRowCache& Cache){
				const int32 RowIndex = FindInteractionRow(Actor, Cache);
				if (RowIndex >= 0){ Clipper.IncludeItemByIndex(RowIndex); }
			};

		IncludeActor(RenameTarget, RenameRowCache);
		IncludeActor(PopupActor, PopupRowCache);

		AActor* DragActor = nullptr;

		// 드래그 소스가 화면 밖으로 나가도 계속 제출
		if (const ImGuiPayload* Payload = ImGui::GetDragDropPayload())
		{
			if (Payload->IsDataType("EDITOR_ACTOR") &&	Payload->DataSize == sizeof(AActor*))
			{
				DragActor =	*static_cast<AActor* const*>(Payload->Data);
			}
		}

		IncludeActor(DragActor, DragRowCache);

		while (Clipper.Step())
		{
			for (int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i)
			{
				DrawActorRow(AllRows[i]);
			}
		}
		
		ImGui::EndTable();
	}

	if (PendingDeleteTarget)
	{
		Editor->SetSelectedActor(PendingDeleteTarget);
		Editor->DeleteSelectedActor();

		PendingDeleteTarget = nullptr;
		FinishRename(false);
	}
	else if (RenameTarget && !bFocusRenameInput && !bRenameInputDrawn)
	{
		FinishRename(true);
	}

	if (PendingReparentSource)
	{
		if (PendingReparentSource->SetParentActor(PendingReparentTarget)) { bRowsDirty = true; }
		PendingReparentSource = nullptr;
		PendingReparentTarget = nullptr;
	}
	
	ImGui::End();
}

void UOutlinerWindow::SetVisibilitySubtree(AActor* Actor, bool bVisible)
{
	if (!Actor)
	{
		return;
	}

	Actor->SetVisibility(bVisible);

	for (AActor* Child : Actor->GetChildActors())
	{
		if (Child)
		{
			SetVisibilitySubtree(Child, bVisible);
		}
	}
}

void UOutlinerWindow::DrawRenameInput(AActor* Actor, const ImVec2& Position, float Width)
{
	ImGui::SameLine();
	ImGui::SetCursorScreenPos(Position);
	ImGui::SetNextItemWidth(Width);

	ImGui::PushID(Actor);

	if (bFocusRenameInput)
	{
		ImGui::SetKeyboardFocusHere();
		bFocusRenameInput = false;
	}

	const bool bEscapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
	const bool bEnter = ImGui::InputText("##Rename", &RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
	
	if (bEscapePressed && (ImGui::IsItemActive() || ImGui::IsItemDeactivated()))
	{
		FinishRename(false);
	}
	else if (bEnter || ImGui::IsItemDeactivated())
	{
		FinishRename(true);
	}

	ImGui::PopID();

	bRenameInputDrawn = true;
}

void UOutlinerWindow::RequestRename(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	for (AActor* Parent = Actor->GetParentActor(); Parent != nullptr; Parent = Parent->GetParentActor())
	{
		if (CollapsedActors.Contains(Parent->GetUUID()))
		{
			CollapsedActors.Remove(Parent->GetUUID());
			bRowsDirty = true;
		}
	}

	RenameTarget = Actor;
	RenameBuffer = Actor->GetName().ToString();
	bFocusRenameInput = true;
}

void UOutlinerWindow::FinishRename(bool bApply)
{
	if (bApply && RenameTarget && !RenameBuffer.empty())
	{
		CachedLabel.Remove(RenameTarget->GetUUID());
		RenameTarget->SetName(FName(RenameBuffer));
	}

	RenameTarget = nullptr;
	bFocusRenameInput = false;
	RenameBuffer.clear();
}

bool UOutlinerWindow::CanReparent(AActor* Source, AActor* Target) const
{
	if (!Source || Source == Target) { return false; }

	UScene* Scene = Editor->GetCurrentScene();
	if (!Scene) { return false; }

	bool bSourceExists = false;
	bool bTargetExists = Target == nullptr; // 루트 이동 허용

	Scene->ForEachActor([&](AActor* Actor)
		{
			bSourceExists |= Actor == Source;
			bTargetExists |= Actor == Target;
		});

	if (!bSourceExists || !bTargetExists) { return false; }

	// 소속 확인 이후에만 포인터 역참조
	for (AActor* Current = Target; Current != nullptr; Current = Current->GetParentActor())
	{
		if (Current == Source) { return false; }
	}

	return true;
}

// 씬 교체시 전체 Row를 재구축
void UOutlinerWindow::RebuildRows(UScene* Scene)
{
	ResetInteractionRowCaches();
	AllRows.Empty();
	Scene->ForEachActor([this](AActor* Actor) {
		if (Actor->GetParentActor() == nullptr) { AppendRows(Actor, 0); }
		});
	bRowsDirty = false;
}

//AllRows에 액터와 뎁스를 묶어서 추가.
void UOutlinerWindow::AppendRows(AActor* Actor, int32 Depth)
{
	AllRows.Add(FOutlinerRow{ Actor,Depth });
	if (CollapsedActors.Contains(Actor->GetUUID())) { return; }
	for (AActor* Child : Actor->GetChildActors()) {
		if (Child) { AppendRows(Child, Depth + 1); }
	}
}

void UOutlinerWindow::DrawActorRow(const FOutlinerRow& Row)
{
	AActor* Actor = Row.Actor;
	const uint32 UUID = Actor->GetUUID();
	const bool bHasChildren = !Actor->GetChildActors().IsEmpty();
	const bool bRenaming = RenameTarget == Actor;

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);

	ImGui::PushID(static_cast<int32>(UUID));

	const float Indent = Row.Depth * ImGui::GetStyle().IndentSpacing;
	if (Indent > 0.0f) { ImGui::Indent(Indent); }

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | 
		ImGuiTreeNodeFlags_FramePadding | 
		ImGuiTreeNodeFlags_NoTreePushOnOpen;

	if (bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_OpenOnDoubleClick;
		ImGui::SetNextItemOpen(!CollapsedActors.Contains(UUID), ImGuiCond_Always);
	}
	else
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (Editor->GetSelectedActor() == Actor)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bRenaming)
	{
		Flags &= ~ImGuiTreeNodeFlags_SpanAvailWidth;
		Flags |= ImGuiTreeNodeFlags_AllowOverlap;
	}

	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float LabelSpacing = ImGui::GetTreeNodeToLabelSpacing();

	const ImVec2 InputPosition(RowStart.x + LabelSpacing, RowStart.y);
	const float InputWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - LabelSpacing);


	const FString& Name = GetCachedName(Actor);
	const bool bExpanded = ImGui::TreeNodeEx("ActorNode", Flags, "%s", bRenaming ? "" : Name.c_str());
	
	if (bHasChildren && ImGui::IsItemToggledOpen())
	{
		if (bExpanded) { CollapsedActors.Remove(UUID); }
		else { CollapsedActors.Add(UUID,true); }
		bRowsDirty = true;
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { Editor->SetSelectedActor(Actor); }

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("EDITOR_ACTOR", &Actor, sizeof(Actor));
		ImGui::TextUnformatted(Name.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("EDITOR_ACTOR"))
		{
			AActor* SourceActor = *static_cast<AActor* const*>(Payload->Data);
			if (CanReparent(SourceActor, Actor))
			{
				PendingReparentSource = SourceActor;
				PendingReparentTarget = Actor;
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginPopupContextItem("ActorContext",ImGuiPopupFlags_MouseButtonRight))
	{
		PopupActor = Actor;
		if (ImGui::IsWindowAppearing()){ Editor->SetSelectedActor(Actor); }
		if (ImGui::MenuItem("Rename", "F2")){ RequestRename(Actor);	}
		if (ImGui::MenuItem("Delete", "Delete")){ PendingDeleteTarget = Actor; }
		ImGui::EndPopup();
	}    
	if (bRenaming){ DrawRenameInput(Actor, InputPosition, InputWidth); }
	
	if (PopupActor == Actor && !ImGui::IsPopupOpen("ActorContext")){ PopupActor = nullptr; }
	
	
	ImGui::TableSetColumnIndex(1);

	if (Editor->GetSelectedActor() == Actor)
	{
		bool bVisible = Actor->IsVisible();
		
		// 함수 시작에서 UUID를 PushID했으므로 추가 PushID 불필요
		if (ImGui::Checkbox("##Visible", &bVisible)){ SetVisibilitySubtree(Actor, bVisible); }
	}

	// 이름 들여쓰기, 열 이동 초기화 후 아이디 Pop.
	ImGui::TableSetColumnIndex(0);
	if (Indent > 0.0f) { ImGui::Unindent(Indent); }
	ImGui::PopID();
}

//이미 캐시된 액터면 캐시에서 액터의 이름을 찾아서 리턴
//아직 추가되지 않은 액터면 캐시에 추가 후 액터의 FName.ToString()을 리턴
const FString& UOutlinerWindow::GetCachedName(AActor* Actor)
{
	const uint32 UUID = Actor->GetUUID();
	if (FString* Name = CachedLabel.Find(UUID)) { return *Name; }
	CachedLabel.Add(UUID, Actor->GetName().ToString());
	return *CachedLabel.Find(UUID);
}

void UOutlinerWindow::ResetSceneCache()
{
	CancelPendingEdits();
	AllRows.Empty();
	CachedLabel.Empty();
	CollapsedActors.Empty();
	bRowsDirty = true;
}

int32 UOutlinerWindow::FindInteractionRow(AActor* Actor, FInteractionRowCache& Cache)
{
	// 같은 대상이고 행 목록도 재구성되지 않았을 때
	if (Cache.Actor == Actor){return Cache.RowIndex;}

	Cache.Actor = Actor;
	Cache.RowIndex = -1;

	// 상호작용이 끝난 경우.
	if (!Actor){ return -1; }

	// 대상이 바뀌었을 때 한 번만 검색한다.
	for (int32 i = 0; i < AllRows.Num(); ++i)
	{
		if (AllRows[i].Actor == Actor)
		{
			Cache.RowIndex = i;
			break;
		}
	}

	return Cache.RowIndex;
}

void UOutlinerWindow::ResetInteractionRowCaches()
{
	RenameRowCache = FInteractionRowCache{};
	PopupRowCache = FInteractionRowCache{};
	DragRowCache = FInteractionRowCache{};
}

void UOutlinerWindow::OnActorDeleting(AActor* Actor)
{
	if (!Actor){ return; }

	for (AActor* Child : Actor->GetChildActors()){ OnActorDeleting(Child); }

	const uint32 UUID = Actor->GetUUID();

	CachedLabel.Remove(UUID);
	CollapsedActors.Remove(UUID);

	if (RenameTarget == Actor){	FinishRename(false); }

	if (PopupActor == Actor) { PopupActor = nullptr; }

	if (PendingDeleteTarget == Actor) { PendingDeleteTarget = nullptr; }

	if (PendingReparentSource == Actor || PendingReparentTarget == Actor)
	{
		PendingReparentSource = nullptr;
		PendingReparentTarget = nullptr;
	}

	bRowsDirty = true;
}

void UOutlinerWindow::CancelPendingEdits()
{
	FinishRename(false);
	PopupActor = nullptr;
	PendingDeleteTarget = nullptr;
	PendingReparentSource = nullptr;
	PendingReparentTarget = nullptr;
	ResetInteractionRowCaches();
}

