#include "pch.h"
#include "GPUTimer.h"

void FGPUTimer::Initialize(ID3D11Device* Device)
{
	Release();

	if (!Device)
	{
		return;
	}

	D3D11_QUERY_DESC DisjointDesc{};
	DisjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

	D3D11_QUERY_DESC TimestampDesc{};
	TimestampDesc.Query = D3D11_QUERY_TIMESTAMP;

	
	for (FFrameQueries& Query : FrameQueries)
	{
		if (FAILED(Device->CreateQuery(&DisjointDesc, Query.DisjointQuery.GetAddressOf())) ||
			FAILED(Device->CreateQuery(&TimestampDesc, Query.TimestampStartQuery.GetAddressOf())) ||
			FAILED(Device->CreateQuery(&TimestampDesc, Query.TimestampEndQuery.GetAddressOf())))
		{
			Release();
			return;
		}
	}

	bInitialized = true;
}

void FGPUTimer::Release()
{
	for (FFrameQueries& Query : FrameQueries)
	{
		Query.DisjointQuery.Reset();
		Query.TimestampStartQuery.Reset();
		Query.TimestampEndQuery.Reset();
		Query.bIssued = false;
	}
	ReadSlot = 0;
	WriteSlot = 0;

	bRecording = false;
	bInitialized = false;
	bHasValidResult = false;

	GPUTimeMs = 0.0f;
	LastResultTime = {};
}

void FGPUTimer::BeginFrame(ID3D11DeviceContext* Context)
{
	if (!bInitialized || !Context)
	{
		return;
	}

	CollectResults(Context);

	// 결과 조회 실패시 리턴.
	if (!bInitialized)
	{
		return;
	}

	FFrameQueries& Query = FrameQueries[WriteSlot];

	if (Query.bIssued)
	{
		return;
	}
	Context->Begin(Query.DisjointQuery.Get());
	Context->End(Query.TimestampStartQuery.Get());

	bRecording = true;

}

void FGPUTimer::EndFrame(ID3D11DeviceContext* Context)
{
	if (!bInitialized || !Context ||!bRecording)
	{
		return;
	}

	FFrameQueries& Query = FrameQueries[WriteSlot];

	Context->End(Query.TimestampEndQuery.Get());
	Context->End(Query.DisjointQuery.Get());

	Query.bIssued = true;
	bRecording = false;

	WriteSlot = (WriteSlot + 1) % QueryBufferCount;
}

void FGPUTimer::CollectResults(ID3D11DeviceContext* Context)
{
	// 한 프레임 내에서 슬롯 개수만큼 확인.
	
	for (int32 i = 0; i < QueryBufferCount; ++i)
	{
		FFrameQueries& Query = FrameQueries[ReadSlot];

		if (!Query.bIssued){ break; }

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT DisjointData{};
		UINT64 StartTime = 0;
		UINT64 EndTime = 0;

		constexpr UINT Flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;

		const HRESULT DisjointResult = Context->GetData(
			Query.DisjointQuery.Get(), &DisjointData, sizeof(DisjointData),	Flags );

		const HRESULT StartResult = Context->GetData(
			Query.TimestampStartQuery.Get(), &StartTime, sizeof(StartTime), Flags );

		const HRESULT EndResult = Context->GetData(
			Query.TimestampEndQuery.Get(), &EndTime, sizeof(EndTime), Flags	);

		// 실패시 타이머 비활성화
		if (FAILED(DisjointResult) || FAILED(StartResult) || FAILED(EndResult))
		{
			bInitialized = false;
			return;
		}

		// 하나라도 미완료시 갱신X
		if (DisjointResult != S_OK || StartResult != S_OK || EndResult != S_OK)
		{
			break;
		}

		//결과 회수, 성공시 시간 갱신
		if (!DisjointData.Disjoint && DisjointData.Frequency > 0 && EndTime >= StartTime)
		{
			const double ElapsedMs = static_cast<double>(EndTime - StartTime) * 1000.0 / static_cast<double>(DisjointData.Frequency);
			GPUTimeMs = static_cast<float>(ElapsedMs);
			bHasValidResult = true;
			LastResultTime = Clock::now();
		}

		// 결과회수는 되었으나 유효하지않은시간일때
		Query.bIssued = false;
		ReadSlot = (ReadSlot + 1) % QueryBufferCount;
	}
}