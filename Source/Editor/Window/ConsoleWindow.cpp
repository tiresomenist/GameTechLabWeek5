#include "pch.h"
#include "ConsoleWindow.h"
#include "Engine/Console.h"
#include "Core/Math/Vector.h"
#include "Engine/Engine.h"
#include "ImGui/imgui.h"

void UConsoleWindow::AddDebugText(FString DebugText)
{
	GEngine::GetInstance()->GetConsole()->Append(DebugText);
}

void UConsoleWindow::AddDebugError(FString ErrorText)
{
	GEngine::GetInstance()->GetConsole()->Append(ErrorText);
}

void UConsoleWindow::Clear()
{
	GEngine::GetInstance()->GetConsole()->Clear();
}

void UConsoleWindow::Copy()
{
	FString ClipBoardText;
	for (uint32 i = 0; i < logs.Num(); i++)
	{
		if (!Filter.PassFilter(logs[i].c_str()))
		{
			continue;
		}
		ClipBoardText += logs[i];
		ClipBoardText += "\n";
	}
	ImGui::SetClipboardText(ClipBoardText.c_str()); // 클립보드로 복사
}

void UConsoleWindow::Render(float DeltaTime)
{
	if (!bOpen)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);

	FConsole* console = GEngine::GetInstance()->GetConsole();
	//그려지는것도 제한걸어야함
	logs = console->Get();

	ImGui::PushStyleColor(
		ImGuiCol_WindowBg,
		ImVec4(0.01f, 0.01f, 0.01f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_TitleBgActive,
		ImVec4(0.01f, 0.01f, 0.01f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_Text,
		ImVec4(1.0f, 0.9f, 0.7f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_Button,
		ImVec4(0.05f, 0.05f, 0.05f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_FrameBg,
		ImVec4(0.05f, 0.05f, 0.05f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_FrameBgHovered,
		ImVec4(0.09f, 0.09f, 0.09f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_ButtonHovered,
		ImVec4(0.09f, 0.09f, 0.09f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_ButtonActive,
		ImVec4(0.05f, 0.05f, 0.05f, 1.0f)
	);

	ImGui::PushStyleColor(
		ImGuiCol_Text,
		ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
	);
	ImGui::Begin(Name.c_str(), &bOpen);
	{
		ImGui::PopStyleColor();
		if (ImGui::Button("Add Debug Text"))
		{
			AddDebugText("[Debug] Test");
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Debug Error"))
		{
			AddDebugError("[Error] Test");
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			Clear();
		}
		ImGui::SameLine();
		if (ImGui::Button("Copy"))
		{
			Copy();
		}
		ImGui::SameLine();
		ImGui::Text("검색: ");
		ImGui::SameLine();
		Filter.Draw("##SearchFilter", 180.f);
		ImGui::Separator();

		ImGui::BeginChild("##consoleLogArea",ImVec2(0,0),true, ImGuiWindowFlags_HorizontalScrollbar);

		bool bWasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();

		for (auto & i : logs)
		{
			if (!Filter.PassFilter(i.c_str()))
			{
				continue;
			}
			ImGui::TextUnformatted(i.c_str());
		}
		if (bWasAtBottom)
		{
			ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor(8);
}