#include "pch.h"
#include "PassExecutor.h"
#include "Engine/Resource/ResourceManager.h"
#include <cassert>

void FPassExecutor::ExecutePass(ID3D11DeviceContext1* Context1, const TArray<FPreparedDraw>& PassDraws, 
	const FPipelineStateCache& PipelineCache, const FConstantBufferManager& CBManager, 
	ID3D11Buffer* ViewConstantBuffer, FRenderSubmissionStats& SubmissionStats)
{
	if (!Context1 || PassDraws.IsEmpty()) return;

	ResetStateCache();
	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	if (ViewConstantBuffer) {
		Context1->VSSetConstantBuffers(2, 1, &ViewConstantBuffer);
		Context1->PSSetConstantBuffers(2, 1, &ViewConstantBuffer);
	}

	for (const FPreparedDraw& Draw : PassDraws) {
		assert(Draw.Source != nullptr);

		// 1. Pipeline State 바인딩 (PSO 변경 시에만 실행)
		if (Draw.PipelineId != m_LastPipelineId) {
			const FPipelineState* State = PipelineCache.GetById(Draw.PipelineId);
			if (State) {
				// Shader & InputLayout
				Context1->VSSetShader(State->VertexShader, nullptr, 0);
				Context1->PSSetShader(State->PixelShader, nullptr, 0);
				Context1->IASetInputLayout(State->InputLayout);


				Context1->IASetPrimitiveTopology(State->Topology);
				CurrentTopology = State->Topology;
				// Pipeline States
				Context1->RSSetState(State->RasterizerState);
				Context1->OMSetDepthStencilState(State->DepthStencilState, 0);

				// Blend State
				FLOAT BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				Context1->OMSetBlendState(State->BlendState, BlendFactor, 0xFFFFFFFF);

				m_LastPipelineId = Draw.PipelineId;
			}
		}

		// 2. Geometry Buffer 바인딩 (VB/IB 변경 시에만 실행)
		if (Draw.MeshPageId != m_LastMeshPageId) {
			FMeshPageBinding PageBinding = GResourceManager::GetInstance()->GetMeshPageBinding(Draw.MeshPageId);
			if (PageBinding.VertexBuffer && PageBinding.IndexBuffer)
			{
				UINT Offset = 0;
				UINT Stride = PageBinding.Stride;

				Context1->IASetVertexBuffers(0, 1, &PageBinding.VertexBuffer, &Stride, &Offset);
				Context1->IASetIndexBuffer(PageBinding.IndexBuffer, PageBinding.IndexFormat, 0);

				m_LastMeshPageId = Draw.MeshPageId;
			}
		}

		// 3. Material Textures & Samplers 바인딩 (Material 변경 시에만 실행)
		if (Draw.MaterialId != m_LastMaterialId) {
			const FMaterial* Material = Draw.Source->Material;
			if (Material)
			{
				// Material SRVs (t0, t1...)
				if (Material->SRV)
				{
					Context1->PSSetShaderResources(0, 1, &Material->SRV);
				}
				// Material Samplers (s0...)
				if (Material->Sampler)
				{
					Context1->PSSetSamplers(0, 1, &Material->Sampler);
				}
			}
			m_LastMaterialId = Draw.MaterialId;
		}

		// Object b0는 매 draw마다 바뀌지만 Material b1은 재질이 바뀔 때만 다시 묶는다.
		const FCBRangeAllocation& ObjCBAlloc = CBManager.GetObjectCBRange(Draw.ObjectConstantIndex);
		ID3D11Buffer* ObjectBuffer = ObjCBAlloc.Buffer;
		UINT ObjectFirst = ObjCBAlloc.FirstConstant;
		UINT ObjectCount = ObjCBAlloc.NumConstants;
		Context1->VSSetConstantBuffers1(0, 1, &ObjectBuffer, &ObjectFirst, &ObjectCount);

		if (Draw.MaterialConstantIndex != m_LastMaterialConstantIndex)
		{
			const FCBRangeAllocation& MatCBAlloc = CBManager.GetMaterialCBRange(Draw.MaterialConstantIndex);
			ID3D11Buffer* MaterialBuffer = MatCBAlloc.Buffer;
			UINT MaterialFirst = MatCBAlloc.FirstConstant;
			UINT MaterialCount = MatCBAlloc.NumConstants;
			Context1->VSSetConstantBuffers1(1, 1, &MaterialBuffer, &MaterialFirst, &MaterialCount);
			Context1->PSSetConstantBuffers1(1, 1, &MaterialBuffer, &MaterialFirst, &MaterialCount);
			m_LastMaterialConstantIndex = Draw.MaterialConstantIndex;
		}

		// 5. DrawIndexed 실행
		const auto& Geo = Draw.Source->Geometry;
		Context1->DrawIndexed(
			Geo.IndexCount,
			Geo.FirstIndex,
			Geo.BaseVertex
		);
		SubmissionStats.RecordIndexedDraw(Geo.IndexCount, CurrentTopology);
	}
}
