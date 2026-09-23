#include "pch.h"
#include "DebugWindow.h"

#include "SolarSystem.h"
#include "Editor/Editor.h"
#include "Engine/Memory/Allocator.h"

void UDebugWindow::Render(float DeltaTime)
{
	if (!bOpen)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(380.0f, 640.0f), ImGuiCond_FirstUseEver);

	size_t AllocationBytes = GAllocator::GetTotalAllocationBytes();
	size_t AllocationCount = GAllocator::GetTotalAllocationCount();

	ImGui::Begin(Name.c_str(), &bOpen, ImGuiWindowFlags_HorizontalScrollbar);
	{
		float MilliSeconds = DeltaTime * 1000;
		ImGui::Text("FPS %.00f (%.00f ms)", 1000 / MilliSeconds, MilliSeconds);
		ImGui::Text("UObject Heap Memory 사용량: %zu바이트", AllocationBytes);
		ImGui::Text("UObject Heap Memory 객체 수: %zu개", AllocationCount);

		ImGui::Separator();

		static char CommandBuffer[64] = "";
		ImGui::Text("Command:");
		ImGui::SameLine();

		static bool bReclaimFocus = false;
		if (bReclaimFocus)
		{
			ImGui::SetKeyboardFocusHere(0);
			bReclaimFocus = false;
		}

		bool bEnter = ImGui::InputText("##ConsoleCmd", CommandBuffer, sizeof(CommandBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		if (bEnter)
		{
			ImGui::SetKeyboardFocusHere(-1);
		}
		ImGui::SameLine();
		bool bExecute = ImGui::Button("실행");

		if (bEnter || bExecute)
		{
			if (_stricmp(CommandBuffer, "stat FPS") == 0)
			{
				Editor->ToggleShowStatFPS();
			}
			else if (_stricmp(CommandBuffer, "stat unit") == 0)
			{
				Editor->ToggleShowStatUnit();
			}
			else if (_stricmp(CommandBuffer, "stat memory") == 0)
			{
				Editor->ToggleShowStatMemory();
			}
			else if (_stricmp(CommandBuffer, "stat none") == 0)
			{
				Editor->HideAllStats();
			}
			else if (_stricmp(CommandBuffer, "stat all") == 0)
			{
				Editor->ShowAllStats();
			}
			CommandBuffer[0] = '\0';
			bReclaimFocus = true;
		}

		if (ImGui::Button("SpawnSolarSystem"))
		{
			SpawnSolarSystem(Editor->GetCurrentScene());
		}
		if (ImGui::Button("발사"))
		{
			LaunchRocket(Editor->GetCurrentScene());
		}
	}
	ImGui::End();
}
