#pragma once

#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>

#include "Engine/Renderer/RenderDataTypes.h"
#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Renderer/Material.h"
struct FShaderResource;

using FPipelineId = uint16_t;
constexpr FPipelineId INVALID_PIPELINE_ID = 0xFFFF;

enum class EPipelinePass : UINT8
{
	Opaque,
	Additive,
	Outline,
	Highlight,
	Gizmo,
	Text,
	Line
};

struct FPipelineKey {
	const FShaderResource* Shader = nullptr;

	EVertexFormat VertexFormat = EVertexFormat::Simple;
	EPipelinePass Pass = EPipelinePass::Opaque;
	EViewModeIndex ViewMode = EViewModeIndex::VMI_Unlit;

	EPrimitiveBlendMode BlendMode = EPrimitiveBlendMode::Opaque;

	bool bTwoSided = false;

	bool operator==(const FPipelineKey& Other) const {
		return Shader == Other.Shader
			&& VertexFormat == Other.VertexFormat
			&& Pass == Other.Pass
			&& ViewMode == Other.ViewMode
			&& BlendMode == Other.BlendMode
			&& bTwoSided == Other.bTwoSided;
	}
};

namespace std {
	template <>
	struct hash<FPipelineKey> {
		size_t operator()(const FPipelineKey& Key) const noexcept {
			size_t HashVal = std::hash<const void*>{}(Key.Shader);
			auto HashCombine = [&HashVal](size_t Value) {
				HashVal ^= Value + 0x9e3779b9 + (HashVal << 6) + (HashVal >> 2);
			};

			HashCombine(static_cast<size_t>(Key.VertexFormat));
			HashCombine(static_cast<size_t>(Key.Pass));
			HashCombine(static_cast<size_t>(Key.ViewMode));
			HashCombine(static_cast<size_t>(Key.BlendMode));
			HashCombine(static_cast<size_t>(Key.bTwoSided));

			return HashVal;
		}
	};
}

struct FPipelineState {
	FPipelineId PipelineId = INVALID_PIPELINE_ID;
	FPipelineKey Key;

	const FShaderResource* Shader = nullptr;

	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11InputLayout* InputLayout = nullptr;

	ID3D11RasterizerState* RasterizerState = nullptr;
	ID3D11BlendState* BlendState = nullptr;
	ID3D11DepthStencilState* DepthStencilState = nullptr;

	D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT StencilRef = 0;
	FLOAT BlendFactor[4] = { 0.0f,0.0f,0.0f,0.0f };
	UINT SampleMask = 0xffffffff;

	bool IsValid() const {
		return VertexShader != nullptr
			&& PixelShader != nullptr
			&& InputLayout != nullptr
			&& RasterizerState != nullptr
			&& DepthStencilState != nullptr;
	}

	void Apply(ID3D11DeviceContext* Context) const {
		Context->IASetInputLayout(InputLayout);
		Context->IASetPrimitiveTopology(Topology);

		Context->VSSetShader(VertexShader, nullptr, 0);
		Context->PSSetShader(PixelShader, nullptr, 0);

		Context->RSSetState(RasterizerState);
		Context->OMSetBlendState(BlendState, BlendFactor, SampleMask);
		Context->OMSetDepthStencilState(DepthStencilState, StencilRef);
	}
};