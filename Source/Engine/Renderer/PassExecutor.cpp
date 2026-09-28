#include "pch.h"
#include "PassExecutor.h"
#include "Engine/Resource/ResourceManager.h"
#include <cassert>

void FPassExecutor::ExecutePass(ID3D11DeviceContext1* Context1, const std::vector<FPreparedDraw>& PassDraws, const FPipelineStateCache& PipelineCache, const FConstantBufferManager& CBManager, ID3D11Buffer* ViewConstantBuffer)
{
	if (!Context1 || PassDraws.empty()) return;

	ResetStateCache();

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

		// 4. Constant Buffer Ranges 바인딩 (D3D11.1 Context1 API 핵심)
		const FCBRangeAllocation& ObjCBAlloc = CBManager.GetObjectCBRange(Draw.ObjectConstantIndex);
		const FCBRangeAllocation& MatCBAlloc = CBManager.GetMaterialCBRange(Draw.MaterialConstantIndex);

		// b0 (Object) & b1 (Material) 슬롯 Range 배열 구성
		ID3D11Buffer* CBBuffers[2] = { ObjCBAlloc.Buffer, MatCBAlloc.Buffer };
		UINT FirstConstants[2] = { ObjCBAlloc.FirstConstant, MatCBAlloc.FirstConstant };
		UINT NumConstants[2] = { ObjCBAlloc.NumConstants, MatCBAlloc.NumConstants };

		// D3D11.1 Partial Constant Buffer Updates 바인딩
		Context1->VSSetConstantBuffers1(0, 2, CBBuffers, FirstConstants, NumConstants);
		Context1->PSSetConstantBuffers1(0, 2, CBBuffers, FirstConstants, NumConstants);

		// 5. DrawIndexed 실행
		const auto& Geo = Draw.Source->Geometry;
		Context1->DrawIndexed(
			Geo.IndexCount,
			Geo.FirstIndex,
			Geo.BaseVertex
		);
	}
}
