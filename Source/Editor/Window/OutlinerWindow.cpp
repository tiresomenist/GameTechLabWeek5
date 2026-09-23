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

	if (!bOpen)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin(Name.c_str(), &bOpen);

	UScene* Scene = Editor->GetCurrentScene();
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
			if (SourceActor != nullptr)
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

		Scene->ForEachActor([this](AActor* Actor)
		{
			if (Actor->GetParentActor() == nullptr)
			{
				DrawActorTree(Actor);
			}
		});
		
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
		PendingReparentSource->SetParentActor(PendingReparentTarget);
		PendingReparentSource = nullptr;
		PendingReparentTarget = nullptr;
	}
	
	ImGui::End();
}

void UOutlinerWindow::DrawActorTree(AActor* Actor)
{
	if (Actor == nullptr)
	{
		return;
	}

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);

	const bool bRenaming = RenameTarget == Actor;
	const bool bHasChildren = !Actor->GetChildActors().IsEmpty();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
	if (bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;
	}
	else
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (bRenaming)
	{
		Flags &= ~ImGuiTreeNodeFlags_SpanAvailWidth;
		Flags |= ImGuiTreeNodeFlags_AllowOverlap;
	}

	if (Editor->GetSelectedActor() == Actor)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	const FString Label = std::format("{}###Actor{}", bRenaming ? "" : Actor->GetName().ToString(), Actor->GetUUID());

	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float LabelSpacing = ImGui::GetTreeNodeToLabelSpacing();

	const ImVec2 InputPosition(RowStart.x + LabelSpacing, RowStart.y);
	const float InputWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - LabelSpacing);

	const bool bNodeOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("EDITOR_ACTOR", &Actor, sizeof(Actor));
		ImGui::TextUnformatted(Actor->GetName().ToString().c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("EDITOR_ACTOR"))
		{
			AActor* SourceActor = *static_cast<AActor**>(Payload->Data);
			if (CanReparent(SourceActor, Actor))
			{
				PendingReparentSource = SourceActor;
				PendingReparentTarget = Actor;
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		Editor->SetSelectedActor(Actor);
	}

	if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight))
	{
		if (ImGui::IsWindowAppearing())
		{
			Editor->SetSelectedActor(Actor);
		}

		if (ImGui::MenuItem("Rename", "F2"))
		{
			RequestRename(Actor);
		}

		if (ImGui::MenuItem("Delete", "Delete"))
		{
			PendingDeleteTarget = Actor;
		}

		ImGui::EndPopup();
	}

	if (bRenaming)
	{
		DrawRenameInput(Actor, InputPosition, InputWidth);
	}

	ImGui::TableSetColumnIndex(1);
	if (Editor->GetSelectedActor() == Actor)
	{
		bool bVisible = Actor->IsVisible();

		ImGui::PushID(Actor);
		if (ImGui::Checkbox("##Visible", &bVisible))
		{
			SetVisibilitySubtree(Actor, bVisible);
		}
		ImGui::PopID();
	}

	ImGui::TableSetColumnIndex(0);

	if (bHasChildren && bNodeOpen)
	{
		for (AActor* Child : Actor->GetChildActors())
		{
			if (Child)
			{
				DrawActorTree(Child);
			}
		}
		ImGui::TreePop();
	}
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

	RenameTarget = Actor;

	const FString CurrentName = Actor->GetName().ToString();
	RenameBuffer = CurrentName;
	bFocusRenameInput = true;
}

void UOutlinerWindow::FinishRename(bool bApply)
{
	if (bApply && RenameTarget && !RenameBuffer.empty())
	{
		RenameTarget->SetName(FName(RenameBuffer));
	}

	RenameTarget = nullptr;
	bFocusRenameInput = false;
	RenameBuffer.clear();
}

bool UOutlinerWindow::CanReparent(AActor* Source, AActor* Target) const
{
	if (!Source || !Target)
	{
		return false;
	}
	
	for (AActor* Current = Target; Current != nullptr; Current = Current->GetParentActor())
	{
		if (Current == Source)
		{
			return false;
		}
	}

	return true;
}
