#include "pch.h"
#include "PipelineStateCache.h"
#include "Engine/Resource/ResourceManager.h"
#include <cassert>

void FPipelineStateCache::Initialize(ID3D11Device* InDevice)
{
	m_Device = InDevice;
}

void FPipelineStateCache::Shutdown()
{
	m_KeyToIdMap.clear();
	m_PipelineStates.clear();
	m_Device = nullptr;
}

const FPipelineState* FPipelineStateCache::GetOrCreate(const FPipelineKey& Key)
{
	auto It = m_KeyToIdMap.find(Key);
	if (It != m_KeyToIdMap.end()) {
		return m_PipelineStates[It->second].get();
	}

	assert(m_PipelineState.size() < INVALID_PIPELINE_ID && "PipelineId overflow!");
	FPipelineId NewId = static_cast<FPipelineId>(m_PipelineStates.size());

	std::unique_ptr<FPipelineState> NewState = CreatePipelineState(Key, NewId);
	if (!NewState || !NewState->IsValid()) {
		assert(false && "Failed to create valid FPipelineState!");
		return nullptr;
	}

	const FPipelineState* RawPtr = NewState.get();
	m_PipelineStates.push_back(std::move(NewState));
	m_KeyToIdMap[Key] = NewId;

	return RawPtr;
}

std::unique_ptr<FPipelineState> FPipelineStateCache::CreatePipelineState(const FPipelineKey& Key, FPipelineId AssignedId)
{
	auto PSO = std::make_unique<FPipelineState>();
	PSO->PipelineId = AssignedId;
	PSO->Key = Key;
	PSO->Shader = Key.Shader;

	GResourceManager* ResourceManager = GResourceManager::GetInstance();

	if (!ResourceManager || !Key.Shader) return nullptr;

	PSO->VertexShader = Key.Shader->VertexShader.Get();
	PSO->PixelShader = Key.Shader->PixelShader.Get();
	PSO->InputLayout = Key.Shader->InputLayout.Get();

	if (Key.ViewMode == EViewModeIndex::VMI_Wireframe) {
		ID3D11PixelShader* WireframePS = ResourceManager->GetWireframePixelShader();

		if (WireframePS) {
			PSO->PixelShader = WireframePS;
		}
	}

	switch (Key.Pass) {
	case EPipelinePass::Line:
		PSO->Topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		break;

	default:
		PSO->Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		break;
	}

	FName RasterizerName("Rasterizer.SolidBack");

    switch (Key.Pass) {
    case EPipelinePass::Outline:
    case EPipelinePass::Highlight:
        RasterizerName = FName("Rasterizer.SolidFront");
        break;

    case EPipelinePass::Line:
    case EPipelinePass::Text:
        RasterizerName = FName("Rasterizer.SolidNone");
        break;

    default:
        RasterizerName = Key.bTwoSided ? FName("Rasterizer.SolidNone") : FName("Rasterizer.SolidBack");
        break;
    }

    if (Key.ViewMode == EViewModeIndex::VMI_Wireframe && Key.Pass != EPipelinePass::Line && Key.Pass != EPipelinePass::Text) {
        RasterizerName = FName("Rasterizer.WireBack");
    }

    PSO->RasterizerState = ResourceManager->GetRasterizerState(RasterizerName);

    switch (Key.Pass) {
    case EPipelinePass::Additive:
        PSO->BlendState = ResourceManager->GetBlendState(FName("Blend.Additive"));
        break;

    case EPipelinePass::Line:
    case EPipelinePass::Text:
    case EPipelinePass::Outline:
    case EPipelinePass::Highlight:
        PSO->BlendState = ResourceManager->GetBlendState(FName("Blend.Alpha"));
        break;

    default:
        PSO->BlendState = nullptr;
        break;
    }

    FName DepthStencilName("Depth.Default");

    switch (Key.Pass)
    {
    case EPipelinePass::Additive:
        DepthStencilName = FName("Depth.Translucent");
        break;

    case EPipelinePass::Outline:
        DepthStencilName = FName("Depth.Outline");
        PSO->StencilRef = 1;
        break;

    case EPipelinePass::Highlight:
        DepthStencilName = FName("Depth.Highlight");
        break;

    case EPipelinePass::Gizmo:
        DepthStencilName = FName("Depth.Gizmo");
        break;

    case EPipelinePass::Text:
        DepthStencilName = FName("Depth.Text");
        break;

    case EPipelinePass::Line:
        DepthStencilName = FName("Depth.Default");
        break;

    default:
        DepthStencilName = FName("Depth.Default");
        break;
    }

    PSO->DepthStencilState = ResourceManager->GetDepthStencilState(DepthStencilName);

    PSO->BlendFactor[0] = 1.0f;
    PSO->BlendFactor[1] = 1.0f;
    PSO->BlendFactor[2] = 1.0f;
    PSO->BlendFactor[3] = 1.0f;
    PSO->SampleMask = 0xffffffff;

    return PSO;
}
