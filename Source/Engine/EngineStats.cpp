#include <pch.h>
#include "EngineStats.h"

#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "Engine/Renderer/Device.h"
#include "Engine/Memory/Allocator.h"


void FEngineStats::UpdateUnitStat(float DeltaTime, float GameTimeMs, float DrawTimeMs, float GPUTimeMs, float GPUWaitMs)
{
    UnitStat.FrameTimeMs = DeltaTime * 1000.0f;
    UnitStat.GameTimeMs = GameTimeMs;
    UnitStat.DrawTimeMs = DrawTimeMs;
    UnitStat.GPUTimeMs = GPUTimeMs;
    UnitStat.GPUWaitMs = GPUWaitMs;
}

void FEngineStats::UpdateMemoryStat()
{
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	size_t PhysicalMemMB = 0;
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
	{
		MemoryStat.PhysicalMemMB = pmc.WorkingSetSize / (1024 * 1024);
		MemoryStat.VirtualMemMB = pmc.PrivateUsage / (1024 * 1024);
	}
	MEMORYSTATUSEX memInfo{};
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	if (GlobalMemoryStatusEx(&memInfo))
	{
		MemoryStat.TotalMemMB = memInfo.ullTotalPhys / (1024 * 1024);
		MemoryStat.UsedMemMB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
	}

	ID3D11Device* Device = GDevice::GetInstance()->GetDevice();
	if (Device)
	{
        Microsoft::WRL::ComPtr<IDXGIDevice> DxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> Adapter;
        if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(DxgiDevice.GetAddressOf()))) &&
            SUCCEEDED(DxgiDevice->GetAdapter(Adapter.GetAddressOf())))
        {
            DXGI_ADAPTER_DESC Desc{};
            if (SUCCEEDED(Adapter->GetDesc(&Desc)))
            {
                MemoryStat.GPUDedicatedMemMB = Desc.DedicatedVideoMemory / (1024 * 1024);
            }
            Microsoft::WRL::ComPtr<IDXGIAdapter3> Adapter3;
            if (SUCCEEDED(Adapter.As(&Adapter3)))
            {
                DXGI_QUERY_VIDEO_MEMORY_INFO VideoInfo{};
                if (SUCCEEDED(Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &VideoInfo)))
                {
                    MemoryStat.GPUVRAMUsedMB = VideoInfo.CurrentUsage / (1024 * 1024);
                }
            }
        }
	}

	MemoryStat.HeapBytes = GAllocator::GetTotalAllocationBytes();
	MemoryStat.ObjectCount = GAllocator::GetTotalAllocationCount();


}