#include "pch.h"
#include "GPUTimer.h"

void FGPUTimer::Initialize(ID3D11Device* Device)
{
	if (!Device)
	{
		return;
	}

	D3D11_QUERY_DESC DisjointDesc{};
	DisjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

	D3D11_QUERY_DESC TimestampDesc{};
	TimestampDesc.Query = D3D11_QUERY_TIMESTAMP;

	for (int32 i = 0; i < QueryBufferCount; ++i)
	{
		Device->CreateQuery(&DisjointDesc, FrameQueries[i].DisjointQuery.GetAddressOf());
		Device->CreateQuery(&TimestampDesc, FrameQueries[i].TimestampStartQuery.GetAddressOf());
		Device->CreateQuery(&TimestampDesc, FrameQueries[i].TimestampEndQuery.GetAddressOf());
		FrameQueries[i].bIssued = false;
	}

	CurrentSlot = 0;
	GPUTimeMs = 0.0f;
	bInitialized = true;
}

void FGPUTimer::Release()
{
	for (int32 i = 0; i < QueryBufferCount; ++i)
	{
		FrameQueries[i].DisjointQuery.Reset();
		FrameQueries[i].TimestampStartQuery.Reset();
		FrameQueries[i].TimestampEndQuery.Reset();
		FrameQueries[i].bIssued = false;
	}
	bInitialized = false;
}

void FGPUTimer::BeginFrame(ID3D11DeviceContext* Context)
{
	if (!bInitialized || !Context)
	{
		return;
	}

	// 이전 슬롯(지난 프레임)의 GPU 시간 회수
	int32 PrevSlot = (CurrentSlot + 1) % QueryBufferCount;
	FFrameQueries& PrevQuery = FrameQueries[PrevSlot];

	if (PrevQuery.bIssued && PrevQuery.DisjointQuery)
	{
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointData{};
		if (Context->GetData(PrevQuery.DisjointQuery.Get(), &DisjointData, sizeof(DisjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
		{
			if (!DisjointData.Disjoint && DisjointData.Frequency > 0)
			{
				UINT64 StartTime = 0;
				UINT64 EndTime = 0;
				if (Context->GetData(PrevQuery.TimestampStartQuery.Get(), &StartTime, sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
					Context->GetData(PrevQuery.TimestampEndQuery.Get(), &EndTime, sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
				{
					if (EndTime >= StartTime)
					{
						float CurGpuMs = static_cast<float>(EndTime - StartTime) / static_cast<float>(DisjointData.Frequency) * 1000.0f;
						GPUTimeMs = (GPUTimeMs * 0.9f) + (CurGpuMs * 0.1f);
					}
				}
			}
			PrevQuery.bIssued = false;
		}
	}

	// 현재 슬롯 쿼리 시작
	FFrameQueries& CurQuery = FrameQueries[CurrentSlot];
	if (CurQuery.DisjointQuery && CurQuery.TimestampStartQuery)
	{
		Context->Begin(CurQuery.DisjointQuery.Get());
		Context->End(CurQuery.TimestampStartQuery.Get());
	}
}

void FGPUTimer::EndFrame(ID3D11DeviceContext* Context)
{
	if (!bInitialized || !Context)
	{
		return;
	}

	FFrameQueries& CurQuery = FrameQueries[CurrentSlot];
	if (CurQuery.DisjointQuery && CurQuery.TimestampEndQuery)
	{
		Context->End(CurQuery.TimestampEndQuery.Get());
		Context->End(CurQuery.DisjointQuery.Get());
		CurQuery.bIssued = true;
	}

	CurrentSlot = (CurrentSlot + 1) % QueryBufferCount;
}
