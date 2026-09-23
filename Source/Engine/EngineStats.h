#pragma once
#include "Core/Core.h"

struct FUnitStat
{
	float FrameTimeMs = 0.0f;
	float GameTimeMs = 0.0f;
	float DrawTimeMs = 0.0f;
	float GPUTimeMs = 0.0f;
	float GPUWaitMs = 0.0f;
};

struct FMemoryStat
{
	// 엔진이 점유중인 실제 물리 RAM
	size_t PhysicalMemMB = 0;
	// 현재 엔진이 할당한 가상 메모리
	size_t VirtualMemMB = 0;
	// 컴퓨터 전체 장착된 총 물리 RAM
	size_t TotalMemMB = 0;
	// 컴퓨터 전체에서 지금 사용 중인 총 RAM
	size_t UsedMemMB = 0;
	// 그래픽 카드 전체 VRAM 용량
	size_t GPUDedicatedMemMB = 0;
	// 현재 엔진이 점유중인 VRAM용량
	size_t GPUVRAMUsedMB = 0;
	// UObject 총 할당 바이트
	size_t HeapBytes = 0;
	// UObject 인스턴스 개수
	size_t ObjectCount = 0;

};

class FEngineStats
{

public:
	const FUnitStat& GetUnitStat()const { return UnitStat; }
	const FMemoryStat& GetMemoryStat()const { return MemoryStat; }

	void UpdateUnitStat(float DeltaTime, float GameTimeMs, float DrawTimeMs, float GPUTimeMs, float GPUWaitMs);
	void UpdateMemoryStat();


	FUnitStat UnitStat;
	FMemoryStat MemoryStat;
};