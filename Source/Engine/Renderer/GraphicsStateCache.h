#pragma once

#include <d3d11.h>
#include <cstdint>
#include <array>
#include "Engine/Renderer/PipelineState.h"

class FGraphicsStateCache {
public:
	FGraphicsStateCache() = default;
	~FGraphicsStateCache() = default;

	void Initialize(ID3D11DeviceContext* InContext);
	void Shutdown();

	void Invalidate();


	void SetPipelineState(const FPipelineState* NewState);

	void SetConstantBuffer(UINT Slot, ID3D11Buffer* Buffer, bool bPixelShader = false);
	void SetShaderResourceView(UINT Slot, ID3D11ShaderResourceView* SRV, bool bPixelShader = false);
	void SetSamplerState(UINT Slot, ID3D11SamplerState* Sampler, bool bPixelShader = false);
private:
	ID3D11DeviceContext* m_Context = nullptr;
	FPipelineId m_CurrentPipelineId = INVALID_PIPELINE_ID;

	ID3D11InputLayout* m_CurrentInputLayout = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY m_CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11VertexShader* m_CurrentVS = nullptr;
	ID3D11PixelShader* m_CurrentPS = nullptr;
	ID3D11RasterizerState* m_CurrentRS = nullptr;
	ID3D11BlendState* m_CurrentBS = nullptr;
	ID3D11DepthStencilState* m_CurrentDSS = nullptr;
	UINT m_CurrentStencilRef = 0;

	static constexpr UINT MAX_CB_SLOTS = 14;
	static constexpr UINT MAX_SRV_SLOTS = 16;
	static constexpr UINT MAX_SAMPLER_SLOTS = 8;

	std::array<ID3D11Buffer*, MAX_CB_SLOTS> m_CurrentVSConstantBuffers = {};
	std::array<ID3D11Buffer*, MAX_CB_SLOTS> m_CurrentPSConstantBuffers = {};
	std::array<ID3D11SamplerState*, MAX_SAMPLER_SLOTS> m_CurrentVSSamplers = {};
	std::array<ID3D11SamplerState*, MAX_SAMPLER_SLOTS> m_CurrentPSSamplers = {};
	std::array<ID3D11ShaderResourceView*, MAX_SRV_SLOTS> m_CurrentPSSRVs = {};
};