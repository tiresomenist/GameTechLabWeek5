#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Core.h"

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

private:
	static constexpr int32 QueryBufferCount = 2;

	struct FFrameQueries
	{
		Microsoft::WRL::ComPtr<ID3D11Query> DisjointQuery;
		Microsoft::WRL::ComPtr<ID3D11Query> TimestampStartQuery;
		Microsoft::WRL::ComPtr<ID3D11Query> TimestampEndQuery;
		bool bIssued = false;
	};

	FFrameQueries FrameQueries[QueryBufferCount];
	int32 CurrentSlot = 0;
	float GPUTimeMs = 0.0f;
	bool bInitialized = false;
};
