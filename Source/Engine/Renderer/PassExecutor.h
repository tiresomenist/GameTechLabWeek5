#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>
#include <vector>

#include "PreparedDraw.h"
#include "PipelineStateCache.h"
#include "ConstantBufferManager.h"
#include "Engine/Util/RenderSubmissionStats.h"

class FPassExecutor {
public:
	FPassExecutor() = default;
	~FPassExecutor() = default;

	void ExecutePass(ID3D11DeviceContext1* Context1, const TArray<FPreparedDraw>& PassDraws, 
		const FPipelineStateCache& PipelineCache, const FConstantBufferManager& CBManager, 
		ID3D11Buffer* ViewConstantBuffer, FRenderSubmissionStats& SubmissionStats);
private:
	uint32 m_LastPipelineId = InvalidRenderId;
	uint32 m_LastMeshPageId = InvalidRenderId;
	uint32 m_LastMaterialId = InvalidRenderId;

	void ResetStateCache() {
		m_LastPipelineId = InvalidRenderId;
		m_LastMeshPageId = InvalidRenderId;
		m_LastMaterialId = InvalidRenderId;
	}
};