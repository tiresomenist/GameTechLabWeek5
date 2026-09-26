#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Core.h"
#include <chrono>

class FGPUTimer
{
public:
	FGPUTimer() = default;
	~FGPUTimer() = default;

	void Initialize(ID3D11Device* Device);
	void Release();

	void BeginFrame(ID3D11DeviceContext* Context);
	void EndFrame(ID3D11DeviceContext* Context);

	float GetGPUTimeMs() const { return GPUTimeMs; }
	bool HasValidResult() const { return bHasValidResult; }
	float GetResultAgeMs() const
	{
		if (!bHasValidResult)
		{
			return -1.0f;
		}

		return std::chrono::duration<float, std::milli>(
			Clock::now() - LastResultTime
		).count();
	}
private:
	using Clock = std::chrono::steady_clock;

	static constexpr int32 QueryBufferCount = 2;

	struct FFrameQueries
	{
		Microsoft::WRL::ComPtr<ID3D11Query> DisjointQuery;
		Microsoft::WRL::ComPtr<ID3D11Query> TimestampStartQuery;
		Microsoft::WRL::ComPtr<ID3D11Query> TimestampEndQuery;
		bool bIssued = false;
	};

	FFrameQueries FrameQueries[QueryBufferCount];

	void CollectResults(ID3D11DeviceContext* Context);

	int32 ReadSlot = 0;	//가장 오래된 미회수 결과 슬롯
	int32 WriteSlot = 0;	//다음 측정 슬롯

	// BeginFrame에서 실제로 측정을 시작했는지 기록합니다.
	bool bRecording = false;	//	이번 프레임에서 시작 타임스탬프를 발행함
	bool bInitialized = false;	//	종료를 발행하고, 결과 회수를 기다림
	bool bHasValidResult = false;	//결과도 회수됨

	float GPUTimeMs = 0.0f;
	Clock::time_point LastResultTime{};

};
