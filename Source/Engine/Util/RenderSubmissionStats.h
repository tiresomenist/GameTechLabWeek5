#pragma once
#include <d3d11.h>
#include "Core/Core.h"

struct FRenderSubmissionCounts
{
    uint32 DrawCalls = 0;
    uint32 Triangles = 0;
    uint32 HZBCandidateCells = 0;
    uint32 HZBRenderCells = 0;
    uint32 HZBOccludedCells = 0;
    bool bHZBEnabled = true;
    bool bHZBDispatched = false;
};

class FRenderSubmissionStats
{
public:
    // 프레임 시작 시 모든 View가 공유할 누적 카운터를 초기화한다.
    void BeginFrame()
    {
        Counts = {};
    }

    // 실제 인덱스 드로우 호출 한 건의 제출량을 누적한다.
    void RecordIndexedDraw(uint32 IndexCount, D3D11_PRIMITIVE_TOPOLOGY Topology)
    {
        ++Counts.DrawCalls;

        // 현재 렌더 경로는 삼각형 목록과 선분 목록을 사용한다.
        // 선분은 드로우 수에만 포함한다.
        if (Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
            Counts.Triangles += static_cast<uint32>(IndexCount) / 3;
    }

    void SetHZBCounts(uint32 CandidateCells, uint32 RenderCells,
        bool bEnabled, bool bDispatched)
    {
        Counts.HZBCandidateCells = CandidateCells;
        Counts.HZBRenderCells = RenderCells;
        Counts.HZBOccludedCells = CandidateCells >= RenderCells
            ? CandidateCells - RenderCells : 0;
        Counts.bHZBEnabled = bEnabled;
        Counts.bHZBDispatched = bDispatched;
    }

    // 현재 프레임에 누적된 뷰포트 제출량을 반환한다.
    const FRenderSubmissionCounts& GetCounts() const
    {
        return Counts;
    }

private:
    FRenderSubmissionCounts Counts;
};
