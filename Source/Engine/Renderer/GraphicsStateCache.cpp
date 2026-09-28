#include "pch.h"
#include "GraphicsStateCache.h"
#include <cassert>

void FGraphicsStateCache::Initialize(ID3D11DeviceContext* InContext)
{
	m_Context = InContext;
	Invalidate();
}

void FGraphicsStateCache::Shutdown()
{
	m_Context = nullptr;
	Invalidate();
}

void FGraphicsStateCache::Invalidate()
{
	m_CurrentPipelineId = INVALID_PIPELINE_ID;

	m_CurrentInputLayout = nullptr;
	m_CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	m_CurrentVS = nullptr;
	m_CurrentPS = nullptr;
	m_CurrentRS = nullptr;
	m_CurrentBS = nullptr;
	m_CurrentDSS = nullptr;
	m_CurrentStencilRef = 0xFFFFFFFF;

	m_CurrentVSConstantBuffers.fill(nullptr);
	m_CurrentPSConstantBuffers.fill(nullptr);
	m_CurrentVSSamplers.fill(nullptr);
	m_CurrentPSSamplers.fill(nullptr);
	m_CurrentPSSRVs.fill(nullptr);
}

void FGraphicsStateCache::SetPipelineState(const FPipelineState* NewState)
{
	if (!NewState || !NewState->IsValid())
	{
		assert(false && "Invalid PipelineState passed to GraphicsStateCache!");
		return;
	}

	if (m_CurrentPipelineId == NewState->PipelineId)
	{
		return;
	}

	m_CurrentPipelineId = NewState->PipelineId;

	if (m_CurrentInputLayout != NewState->InputLayout)
	{
		m_CurrentInputLayout = NewState->InputLayout;
		m_Context->IASetInputLayout(m_CurrentInputLayout);
	}

	if (m_CurrentTopology != NewState->Topology)
	{
		m_CurrentTopology = NewState->Topology;
		m_Context->IASetPrimitiveTopology(m_CurrentTopology);
	}

	if (m_CurrentVS != NewState->VertexShader)
	{
		m_CurrentVS = NewState->VertexShader;
		m_Context->VSSetShader(m_CurrentVS, nullptr, 0);
	}

	if (m_CurrentPS != NewState->PixelShader)
	{
		m_CurrentPS = NewState->PixelShader;
		m_Context->PSSetShader(m_CurrentPS, nullptr, 0);
	}

	if (m_CurrentRS != NewState->RasterizerState)
	{
		m_CurrentRS = NewState->RasterizerState;
		m_Context->RSSetState(m_CurrentRS);
	}

	if (m_CurrentBS != NewState->BlendState)
	{
		m_CurrentBS = NewState->BlendState;
		m_Context->OMSetBlendState(m_CurrentBS, NewState->BlendFactor, NewState->SampleMask);
	}

	if (m_CurrentDSS != NewState->DepthStencilState || m_CurrentStencilRef != NewState->StencilRef)
	{
		m_CurrentDSS = NewState->DepthStencilState;
		m_CurrentStencilRef = NewState->StencilRef;
		m_Context->OMSetDepthStencilState(m_CurrentDSS, m_CurrentStencilRef);
	}
}

void FGraphicsStateCache::SetConstantBuffer(UINT Slot, ID3D11Buffer* Buffer, bool bPixelShader)
{
	assert(Slot < MAX_CB_SLOTS);

	if (bPixelShader) {
		if (m_CurrentPSConstantBuffers[Slot] != Buffer) {
			m_CurrentPSConstantBuffers[Slot] = Buffer;
			m_Context->PSSetConstantBuffers(Slot, 1, &Buffer);
		}
	}
	else {
		if (m_CurrentVSConstantBuffers[Slot] != Buffer) {
			m_CurrentVSConstantBuffers[Slot] = Buffer;
			m_Context->VSSetConstantBuffers(Slot, 1, &Buffer);
		}
	}
}

void FGraphicsStateCache::SetShaderResourceView(UINT Slot, ID3D11ShaderResourceView* SRV, bool bPixelShader)
{
	assert(Slot < MAX_SRV_SLOTS);

	if (bPixelShader) {
		if (m_CurrentPSSRVs[Slot] != SRV) {
			m_CurrentPSSRVs[Slot] = SRV;
			m_Context->PSSetShaderResources(Slot, 1, &SRV);
		}
	}
}

void FGraphicsStateCache::SetSamplerState(UINT Slot, ID3D11SamplerState* Sampler, bool bPixelShader)
{
	assert(Slot < MAX_SAMPLER_SLOTS);

	if (bPixelShader) {
		if (m_CurrentPSSamplers[Slot] != Sampler) {
			m_CurrentPSSamplers[Slot] = Sampler;
			m_Context->PSSetSamplers(Slot, 1, &Sampler);
		}
	}
	else {
		if (m_CurrentVSSamplers[Slot] != Sampler) {
			m_CurrentVSSamplers[Slot] = Sampler;
			m_Context->VSSetSamplers(Slot, 1, &Sampler);
		}
	}
}
