#pragma once
#include "Core/Core.h"
#include <Windows.h>

enum class EDebugCpuStat : uint32
{
    Frame, SceneTick, EditorTick, GizmoTick, BVHUpdate, BVHBuild, GridBuild,
    Gather, GatherPrepare, GatherCreate, GatherFinalize, GatherIcons,
    BuildPasses, Sort, ConstantUpload, Opaque, OtherPasses, Readback, Present, Count
};

constexpr uint32 DebugCpuStatCount = static_cast<uint32>(EDebugCpuStat::Count);

struct FDebugCpuSample
{
    double AverageMs = 0.0;
    double PeakMs = 0.0;
    double CallsPerFrame = 0.0;
};

// HZB 셀 필터를 통과해 실제 수집 함수에 들어온 프리미티브만 집계합니다.
struct FDebugGatherCounts
{
    uint32 Visited = 0;
    uint32 Hidden = 0;
    uint32 FrustumRejected = 0;
    uint32 Empty = 0;
    uint32 Objects = 0;
    uint32 Requests = 0;
};

struct FDebugRenderCounts
{
    uint32 Requests = 0;
    uint32 CandidateCells = 0;
    uint32 RenderCells = 0;
    uint32 Views = 0;
    uint32 HistoryViews = 0;
    FDebugGatherCounts Gather;
};

// 메인 스레드에서만 사용하며 고정된 항목을 프레임별로 합산합니다.
class FDebugCpuStats
{
public:
    static FDebugCpuStats& Get();
    void SetEnabled(bool bValue) { bEnabled = bValue; }
    bool IsEnabled() const { return bEnabled; }
    bool IsCollecting() const { return bCollecting; }
    // 상세 계측 설정도 프레임 경계에서 반영하여 여러 View가 같은 모드를 사용합니다.
    void SetGatherDetailEnabled(bool bValue) { bGatherDetailEnabled = bValue; }
    bool IsGatherDetailEnabled() const { return bGatherDetailEnabled; }
    bool IsCollectingGatherDetail() const { return bCollectingGatherDetail; }
    void BeginFrame();
    void EndFrame(float DeltaTime);
    void AddTime(EDebugCpuStat Stat, double Milliseconds);
    void AddRequests(uint32 Count);
    void AddVisibility(uint32 Candidates, uint32 Rendered, bool bUsedHistory);
    void AddGatherCounts(const FDebugGatherCounts& Counts);
    const FDebugCpuSample& GetSample(EDebugCpuStat Stat) const { return Samples[static_cast<uint32>(Stat)]; }
    const FDebugRenderCounts& GetRenderCounts() const { return CompletedCounts; }
    double GetFPS() const { return FPS; }
    bool HasSamples() const { return bHasSamples; }
    double GetMillisecondsPerTick() const { return MillisecondsPerTick; }

private:
    FDebugCpuStats();
    struct FAccumulator
    {
        double TotalMs = 0.0;
        double PeakMs = 0.0;
        uint32 Calls = 0;
    };
    FAccumulator Frame[DebugCpuStatCount]{};
    FAccumulator Window[DebugCpuStatCount]{};
    FDebugCpuSample Samples[DebugCpuStatCount]{};
    FDebugRenderCounts FrameCounts;
    FDebugRenderCounts CompletedCounts;
    double MillisecondsPerTick = 0.0;
    double WindowSeconds = 0.0;
    double FPS = 0.0;
    uint32 WindowFrames = 0;
    bool bEnabled = false;
    bool bCollecting = false;
    bool bWasEnabled = false;
    bool bHasSamples = false;
    bool bGatherDetailEnabled = false;
    bool bCollectingGatherDetail = false;
    bool bWasGatherDetailEnabled = false;
};

// 디버그창이 꺼진 프레임에는 시계를 읽지 않습니다.
class FScopedDebugCpuTime
{
public:
    explicit FScopedDebugCpuTime(EDebugCpuStat InStat);
    ~FScopedDebugCpuTime() { Finish(); }
    void Finish();
    FScopedDebugCpuTime(const FScopedDebugCpuTime&) = delete;
    FScopedDebugCpuTime& operator=(const FScopedDebugCpuTime&) = delete;
private:
    FDebugCpuStats& Stats;
    EDebugCpuStat Stat;
    LARGE_INTEGER Start{};
    bool bActive;
};
