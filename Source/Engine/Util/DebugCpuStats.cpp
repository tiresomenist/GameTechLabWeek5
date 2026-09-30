#include "pch.h"
#include "DebugCpuStats.h"

// 모든 계측 지점과 디버그창이 같은 프레임 통계를 사용합니다.
FDebugCpuStats& FDebugCpuStats::Get()
{
    static FDebugCpuStats Instance;
    return Instance;
}

// 고해상도 타이머의 변환 계수를 한 번만 구합니다.
FDebugCpuStats::FDebugCpuStats()
{
    LARGE_INTEGER Frequency{};
    QueryPerformanceFrequency(&Frequency);
    MillisecondsPerTick = 1000.0 / static_cast<double>(Frequency.QuadPart);
}

// 메뉴에서 변경한 활성 상태를 다음 프레임 경계에 반영합니다.
void FDebugCpuStats::BeginFrame()
{
    bCollecting = bEnabled;
    if (bEnabled && !bWasEnabled)
    {
        for (uint32 Index = 0; Index < DebugCpuStatCount; ++Index)
        {
            Window[Index] = {};
            Samples[Index] = {};
        }
        CompletedCounts = {};
        WindowSeconds = 0.0;
        WindowFrames = 0;
        FPS = 0.0;
        bHasSamples = false;
    }
    bWasEnabled = bEnabled;
    if (!bCollecting) return;
    for (FAccumulator& Item : Frame) Item = {};
    FrameCounts = {};
}

// 여러 View의 시간을 프레임별로 합친 뒤 0.25초마다 표시값을 갱신합니다.
void FDebugCpuStats::EndFrame(float DeltaTime)
{
    if (!bCollecting) return;
    bCollecting = false;
    CompletedCounts = FrameCounts;
    ++WindowFrames;
    WindowSeconds += (std::max)(static_cast<double>(DeltaTime), 0.0);
    for (uint32 Index = 0; Index < DebugCpuStatCount; ++Index)
    {
        Window[Index].TotalMs += Frame[Index].TotalMs;
        Window[Index].PeakMs = (std::max)(Window[Index].PeakMs, Frame[Index].TotalMs);
        Window[Index].Calls += Frame[Index].Calls;
    }
    if (WindowSeconds < 0.25) return;
    for (uint32 Index = 0; Index < DebugCpuStatCount; ++Index)
    {
        Samples[Index] = {Window[Index].TotalMs / WindowFrames,
            Window[Index].PeakMs, static_cast<double>(Window[Index].Calls) / WindowFrames};
        Window[Index] = {};
    }
    FPS = WindowFrames / WindowSeconds;
    WindowFrames = 0;
    WindowSeconds = 0.0;
    bHasSamples = true;
}

// 같은 프레임에서 여러 번 호출된 구간의 시간과 횟수를 누적합니다.
void FDebugCpuStats::AddTime(EDebugCpuStat Stat, double Milliseconds)
{
    if (!bCollecting) return;
    FAccumulator& Item = Frame[static_cast<uint32>(Stat)];
    Item.TotalMs += Milliseconds;
    ++Item.Calls;
}

// 수집된 일반 프리미티브 요청 수를 모든 View에 걸쳐 합산합니다.
void FDebugCpuStats::AddRequests(uint32 Count)
{
    if (bCollecting) FrameCounts.Requests += Count;
}

// 이전 HZB 이력의 사용 여부를 Dispatch 여부와 구분해서 기록합니다.
void FDebugCpuStats::AddVisibility(uint32 Candidates, uint32 Rendered, bool bUsedHistory)
{
    if (!bCollecting) return;
    FrameCounts.CandidateCells += Candidates;
    FrameCounts.RenderCells += Rendered;
    ++FrameCounts.Views;
    if (bUsedHistory) ++FrameCounts.HistoryViews;
}

// 현재 프레임이 계측 대상이면 구간 시작 시각을 기록합니다.
FScopedDebugCpuTime::FScopedDebugCpuTime(EDebugCpuStat InStat)
    : Stats(FDebugCpuStats::Get()), Stat(InStat), bActive(Stats.IsCollecting())
{
    if (bActive) QueryPerformanceCounter(&Start);
}

// 조기 반환에서도 시간을 반영하며 명시적으로 종료한 구간은 중복 기록하지 않습니다.
void FScopedDebugCpuTime::Finish()
{
    if (!bActive) return;
    LARGE_INTEGER End{};
    QueryPerformanceCounter(&End);
    Stats.AddTime(Stat, static_cast<double>(End.QuadPart - Start.QuadPart) * Stats.GetMillisecondsPerTick());
    bActive = false;
}
