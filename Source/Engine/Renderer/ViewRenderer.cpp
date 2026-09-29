#include "pch.h"
#include "ViewRenderer.h"
#include "Engine/Component/CameraComponent.h"
#include "Engine/Renderer/Context.h"
#include "Engine/Renderer/RenderUtil.h"
#include "Engine/Renderer/Text/TextMeshBuilder.h"
#include "Engine/Renderer/ViewRenderData.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Log.h"

#include <format>
#include <stdexcept>

namespace
{
	// 이전 프레임 깊이를 사용할 수 있도록 카메라와 출력 영역이 같은지 확인
	bool IsSameHZBView(const FRenderViewSnapshot& A, const FRenderViewSnapshot& B)
	{
		if (A.ViewMode != B.ViewMode) return false;
		const D3D11_VIEWPORT& VA = A.Viewport;
		const D3D11_VIEWPORT& VB = B.Viewport;
		if (VA.TopLeftX != VB.TopLeftX || VA.TopLeftY != VB.TopLeftY ||
			VA.Width != VB.Width || VA.Height != VB.Height ||
			VA.MinDepth != VB.MinDepth || VA.MaxDepth != VB.MaxDepth) return false;

		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				if (A.ViewProjection.M[Row][Column] != B.ViewProjection.M[Row][Column]) return false;
			}
		}
		return true;
	}

	struct FConstants
	{
		FMatrix World;
		FMatrix ViewProjection;
	};

	struct FGridConstants
	{
		FVector CameraPos;
		int GridPlaneType;
	};

	void CheckHR(HRESULT Result)
	{
		if (FAILED(Result))
			throw std::runtime_error(std::format("D3D resource creation failed: {}", Result));
	}

	TArray<FHZBCellData> BuildHZBCellData(const TArray<FVisibleGridCell>& VisibleGridCells)
	{
		TArray<FHZBCellData> Result;
		Result.Reserve(VisibleGridCells.Num());
		for (const FVisibleGridCell& Cell : VisibleGridCells)
		{
			FHZBCellData Data{};
			Data.BoundsMin = FVector4(Cell.Bounds.Min, 1.0f);
			Data.BoundsMax = FVector4(Cell.Bounds.Max, 1.0f);
			Result.Add(Data);
		}
		return Result;
	}
}
void FViewRenderer::Create(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	if (D3DDevice)
	{
		throw std::logic_error("View renderer is already initialized");
	}
	if (!InDevice || !InContext)
	{
		throw std::invalid_argument("View renderer requires a device and context");
	}

	D3DDevice = InDevice;
	DeviceContext = InContext;
	CreateRasterizerState();
	if (!CreateShaders()) throw std::runtime_error("Required render shaders are missing");
	CreateConstantBuffer();
	CreateAlphaBlendState();
	CreateDepthStencilStates();
	CreateTextResources();
	PipelineStateCache.Initialize(InDevice);
	if (D3DDevice) {
		CBRingBuffer.Initialize(D3DDevice, 32 * 1024 * 1024);
	}
}

void FViewRenderer::Shutdown()
{
	CBRingBuffer.Shutdown();
	CBManager.Clear();
	PipelineStateCache.Shutdown();

	LineBatcher.Clear();
	LineBatcher.Release();
	for (auto& Entry : HZBViewStates)
	{
		Entry.second.Culler.Release();
	}
	HZBViewStates.Empty();
	PrimitiveVisibility.Empty();
	ReleaseConstantBuffer();
	ReleaseShaders();
	ReleaseRasterizerState();
	ReleaseAlphaBlendState();
	ReleaseDepthStencilStates();
	ReleaseTextResources();
	DeviceContext = nullptr;
	D3DDevice = nullptr;
}

bool FViewRenderer::CreateShaders()
{
	GResourceManager& Resources = *GResourceManager::GetInstance();

	const FShaderResource* ColorShader = Resources.GetShader(FName("Mesh.Color"));
	const FShaderResource* SharedHighlight = Resources.GetShader(FName("Editor.Highlight"));
	const FShaderResource* SharedGrid = Resources.GetShader(FName("Editor.Grid"));
	const FShaderResource* SharedBatchLine = Resources.GetShader(FName("Editor.BatchLine"));
	ID3D11PixelShader* SharedWireframe = Resources.GetWireframePixelShader();

	const TArray<const FShaderResource*> RequiredShaders
	{
		ColorShader, SharedHighlight, SharedGrid, SharedBatchLine
	};
	for (const FShaderResource* Shader : RequiredShaders)
	{
		if (!Shader || !Shader->VertexShader || !Shader->PixelShader || !Shader->InputLayout)
		{
			return false;
		}
	}
	if (!SharedWireframe)
	{
		return false;
	}

	// ResourceManager가 소유한 자원을 참조한다.
	SimpleShader = ColorShader;
	HighlightShader = SharedHighlight;
	GridShader = SharedGrid;
	BatchLineShader = SharedBatchLine;
	WireframePixelShader = SharedWireframe;
	return true;
}

void FViewRenderer::ReleaseShaders()
{
	SimpleShader = nullptr;
	WireframePixelShader = nullptr;
	HighlightShader = nullptr;
	GridShader = nullptr;
	BatchLineShader = nullptr;
}

void FViewRenderer::CreateConstantBuffer()
{
	D3D11_BUFFER_DESC constantbufferdesc = {};
	constantbufferdesc.ByteWidth = (sizeof(FConstants) + 0xf) & 0xfffffff0;
	constantbufferdesc.Usage = D3D11_USAGE_DYNAMIC;
	constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	constantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	CheckHR(D3DDevice->CreateBuffer(&constantbufferdesc, nullptr, TransformConstantBuffer.GetAddressOf()));

	D3D11_BUFFER_DESC gridconstantbufferdesc = {};
	gridconstantbufferdesc.ByteWidth = (sizeof(FGridConstants) + 0xf) & 0xfffffff0;
	gridconstantbufferdesc.Usage = D3D11_USAGE_DYNAMIC;
	gridconstantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	gridconstantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	CheckHR(D3DDevice->CreateBuffer(&gridconstantbufferdesc, nullptr, GridConstantBuffer.GetAddressOf()));

	D3D11_BUFFER_DESC MaterialDesc{};
	MaterialDesc.ByteWidth = (sizeof(FTextureDrawConstants) + 15u) & ~15u;
	MaterialDesc.Usage = D3D11_USAGE_DYNAMIC;
	MaterialDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	MaterialDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	CheckHR(D3DDevice->CreateBuffer(
		&MaterialDesc, nullptr, MaterialConstantBuffer.GetAddressOf()));

	D3D11_BUFFER_DESC HZBDesc{};
	HZBDesc.ByteWidth = sizeof(FHZBCullConstants);
	HZBDesc.Usage = D3D11_USAGE_DEFAULT;
	HZBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	CheckHR(D3DDevice->CreateBuffer(
		&HZBDesc, nullptr, HZBCullConstantBuffer.GetAddressOf()));
}

void FViewRenderer::ReleaseConstantBuffer()
{
    TransformConstantBuffer.Reset();
    GridConstantBuffer.Reset();
    MaterialConstantBuffer.Reset();
    HZBCullConstantBuffer.Reset();
}

void FViewRenderer::CreateRasterizerState()
{
	GResourceManager& Resources = *GResourceManager::GetInstance();

	DefaultRasterizerState = Resources.GetRasterizerState(FName("Rasterizer.SolidBack"));

	CullNoneRasterizerState = Resources.GetRasterizerState(FName("Rasterizer.SolidNone"));

	CullFrontRasterizerState = Resources.GetRasterizerState(FName("Rasterizer.SolidFront"));

	WireframeRasterizerState = Resources.GetRasterizerState(FName("Rasterizer.WireBack"));

	if (!DefaultRasterizerState || !CullNoneRasterizerState || !CullFrontRasterizerState || !WireframeRasterizerState)
	{
		throw std::runtime_error("Required rasterizer states are missing");
	}
}

void FViewRenderer::ReleaseRasterizerState()
{
	// 공유 자원은 ResourceManager가 해제한다.
	DefaultRasterizerState = nullptr;
	CullNoneRasterizerState = nullptr;
	CullFrontRasterizerState = nullptr;
	WireframeRasterizerState = nullptr;
}

void FViewRenderer::CreateAlphaBlendState()
{
	GResourceManager& Resources = *GResourceManager::GetInstance();
	AlphaBlendState = Resources.GetBlendState(FName("Blend.Alpha"));
	AdditiveBlendState = Resources.GetBlendState(FName("Blend.Additive"));
	if (!AlphaBlendState || !AdditiveBlendState)
	{
		throw std::runtime_error("Required blend states are missing");
	}
}

void FViewRenderer::ReleaseAlphaBlendState()
{
	// 공유 자원은 ResourceManager가 해제한다.
	AlphaBlendState = nullptr;
	AdditiveBlendState = nullptr;
}

void FViewRenderer::CreateDepthStencilStates()
{
	GResourceManager& Resources = *GResourceManager::GetInstance();
	DefaultDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Default"));
	GizmoDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Gizmo"));
	HighlightDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Highlight"));
	TranslucentDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Translucent"));
	TextDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Text"));
	StencilWriteDepthStencilState = Resources.GetDepthStencilState(FName("Depth.StencilWrite"));
	OutlineDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Outline"));
	const TArray<ID3D11DepthStencilState*> RequiredStates
	{
		DefaultDepthStencilState,
		GizmoDepthStencilState,
		HighlightDepthStencilState,
		TranslucentDepthStencilState,
		TextDepthStencilState,
		StencilWriteDepthStencilState,
		OutlineDepthStencilState,
	};

	for (ID3D11DepthStencilState* State : RequiredStates)
	{
		if (!State)
		{
			throw std::runtime_error("Required depth stencil states are missing");
		}
	}
}

void FViewRenderer::ReleaseDepthStencilStates()
{
	// 공유 자원은 ResourceManager가 해제한다.
	DefaultDepthStencilState = nullptr;
	GizmoDepthStencilState = nullptr;
	HighlightDepthStencilState = nullptr;
	TranslucentDepthStencilState = nullptr;
	TextDepthStencilState = nullptr;
	StencilWriteDepthStencilState = nullptr;
	OutlineDepthStencilState = nullptr;
}

void FViewRenderer::CreateTextResources()
{
	// 텍스트 셰이더, 샘플러를 GResource매니저에서 받아옴
	GResourceManager& Resources = *GResourceManager::GetInstance();

	const FShaderResource* SharedTextShader = Resources.GetShader(FName("Editor.Text"));

	ID3D11SamplerState* SharedFontSampler = Resources.GetSampler(FName("Font.LinearClamp"));

	if (!SharedTextShader ||!SharedTextShader->VertexShader ||!SharedTextShader->PixelShader ||
		!SharedTextShader->InputLayout ||!SharedFontSampler)
	{
		throw std::runtime_error("Required text render resources are missing");
	}

	TextShader = SharedTextShader;
	FontSamplerState = SharedFontSampler;

	// Vertex Buffer: 매 프레임 내용이 바뀌므로 DYNAMIC, 고정 용량으로 1회만 생성
	D3D11_BUFFER_DESC VBDesc = {};
	VBDesc.ByteWidth = sizeof(FVertexTexture) * MaxTextVertices;
	VBDesc.Usage = D3D11_USAGE_DYNAMIC;
	VBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	CheckHR(D3DDevice->CreateBuffer(&VBDesc, nullptr, TextVertexBuffer.GetAddressOf()));

	// Index Buffer: quad 패턴(0,1,2,0,2,3)이 항상 동일하므로 1회 IMMUTABLE 생성
	const UINT MaxQuads = MaxTextVertices / 4;
	TArray<uint32> Indices;
	Indices.Reserve(MaxQuads * 6);
	for (UINT q = 0; q < MaxQuads; ++q)
	{
		const uint32 Base = q * 4;
		Indices.Add(Base + 0);
		Indices.Add(Base + 1);
		Indices.Add(Base + 2);
		Indices.Add(Base + 0);
		Indices.Add(Base + 2);
		Indices.Add(Base + 3);
	}

	D3D11_BUFFER_DESC IBDesc = {};
	IBDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * Indices.Num());
	IBDesc.Usage = D3D11_USAGE_IMMUTABLE;
	IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA IBData = { Indices.GetData() };
	CheckHR(D3DDevice->CreateBuffer(&IBDesc, &IBData, TextIndexBuffer.GetAddressOf()));
}

void FViewRenderer::ReleaseTextResources()
{
	// 공유 자원은 참조만 비운다.
	TextShader = nullptr;
	FontSamplerState = nullptr;

	TextVertexBuffer.Reset();
	TextIndexBuffer.Reset();
}

void FViewRenderer::UpdateTextVertexBuffer(TArray<FVertexTexture>& Vertices)
{
	if (Vertices.Num() == 0) return;

	const UINT CopyCount = (static_cast<UINT>(Vertices.Num()) < MaxTextVertices) ? static_cast<UINT>(Vertices.Num()) : MaxTextVertices;

	D3D11_MAPPED_SUBRESOURCE Mapped;
	DeviceContext->Map(TextVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
	memcpy(Mapped.pData, Vertices.GetData(), sizeof(FVertexTexture) * CopyCount);
	DeviceContext->Unmap(TextVertexBuffer.Get(), 0);
}

void FViewRenderer::RenderText(UINT IndexCount)
{
	if (IndexCount == 0) return;

	UINT Stride = sizeof(FVertexTexture);
	UINT Offset = 0;

	BindShader(*TextShader);
	DeviceContext->IASetVertexBuffers(0, 1, TextVertexBuffer.GetAddressOf(), &Stride, &Offset);
	DeviceContext->IASetIndexBuffer(TextIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	DeviceContext->VSSetConstantBuffers(0, 1, TransformConstantBuffer.GetAddressOf());

	FFontAtlas* FontAtlas =	GResourceManager::GetInstance()->GetDefaultFont();
	if (!FontAtlas) return;

	ID3D11ShaderResourceView* SRV = FontAtlas->GetSRV();
	DeviceContext->PSSetShaderResources(0, 1, &SRV);
	DeviceContext->PSSetSamplers(0, 1, &FontSamplerState);

	DeviceContext->RSSetState(CullNoneRasterizerState);

	float BlendFactor[4] = { 0, 0, 0, 0 };
	DeviceContext->OMSetBlendState(AlphaBlendState, BlendFactor, 0xffffffff);
	DeviceContext->OMSetDepthStencilState(TextDepthStencilState, 0);

	DeviceContext->DrawIndexed(IndexCount, 0, 0);
	SubmissionStats.RecordIndexedDraw(IndexCount, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void FViewRenderer::RenderPrimitive(const FPrimitiveRenderData& Data, EViewModeIndex InViewMode, bool bWriteStencil)
{
	if (!Data.Material)	return;
	if (Data.Geometry.MeshPageId == InvalidRenderId || Data.Geometry.IndexCount == 0) return;
	if (!BindMaterial(*Data.Material)) return;
	UpdateMaterialConstants(Data);
	BindPrimitiveBuffers(Data);

	const bool bWireframe = InViewMode == EViewModeIndex::VMI_Wireframe;

	if (bWireframe) {
		DeviceContext->PSSetShader(WireframePixelShader, nullptr, 0);
	}

	ID3D11RasterizerState* RasterizerState = bWireframe ? WireframeRasterizerState : (Data.Material->bTwoSided ? CullNoneRasterizerState : DefaultRasterizerState);

	DeviceContext->RSSetState(RasterizerState);

	const bool bAdditive = Data.Material->BlendMode == EPrimitiveBlendMode::Additive;

	if (bAdditive) {
		DeviceContext->OMSetBlendState(AdditiveBlendState, nullptr, 0xffffffff);
		DeviceContext->OMSetDepthStencilState(TranslucentDepthStencilState, 0);
	}
	else {
		DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		if (bWriteStencil) {
			DeviceContext->OMSetDepthStencilState(StencilWriteDepthStencilState, 1);
		}
		else {
			DeviceContext->OMSetDepthStencilState(DefaultDepthStencilState, 0);
		}
	}

	DeviceContext->DrawIndexed(Data.Geometry.IndexCount, Data.Geometry.FirstIndex, Data.Geometry.BaseVertex);
}

// View별 유효한 판정으로 가시성 배열을 만들고 다음 GPU 판정을 제출한다.
void FViewRenderer::PreparePrimitiveVisibility(const FViewRenderData& Data, const FHZBViewInput& HZB)
{
	PrimitiveVisibility.SetNum(Data.Primitives.Num());
	for (uint32& Visible : PrimitiveVisibility) Visible = 1;
	uint32 CellCount = 0;
	uint32 OccludedCellCount = 0;
	uint32 OccludedPrimitiveCount = 0;
	bool bDispatched = false;

	if (!bEnableHZBOcclusion)
	{
		SubmissionStats.SetHZBCounts(static_cast<uint32>(Data.Primitives.Num()), CellCount,
			OccludedCellCount, OccludedPrimitiveCount, false, bDispatched);
		return;
	}

	FHZBViewState& State = HZBViewStates[HZB.ViewId];
	const bool bCanUseHistory = HZB.bValid && HZB.Texture && HZB.MipCount > 0 &&
		State.bHasPreviousView && State.PreviousFrameIndex + 1 == HZB.FrameIndex &&
		State.PreviousGeneration == HZB.Generation && State.PreviousScene == HZB.Scene &&
		IsSameHZBView(State.PreviousView, Data.View);

	State.PreviousView = Data.View;
	State.PreviousScene = HZB.Scene;
	State.PreviousFrameIndex = HZB.FrameIndex;
	State.PreviousGeneration = HZB.Generation;
	State.bHasPreviousView = true;
	if (!bCanUseHistory || !D3DDevice || !DeviceContext)
	{
		// 첫 프레임이나 카메라 변경 이전의 결과는 사용하지 않음
		State.Culler.Release();
		SubmissionStats.SetHZBCounts(static_cast<uint32>(Data.Primitives.Num()), CellCount,
			OccludedCellCount, OccludedPrimitiveCount, true, bDispatched);
		return;
	}

	GResourceManager& Resources = *GResourceManager::GetInstance();
	ID3D11ComputeShader* Shader = Resources.GetComputeShader(FName("HZB.CullCells"));
	if (!Shader || !HZBCullConstantBuffer)
	{
		State.Culler.Release();
		SubmissionStats.SetHZBCounts(static_cast<uint32>(Data.Primitives.Num()), CellCount,
			OccludedCellCount, OccludedPrimitiveCount, true, bDispatched);
		return;
	}

	State.Culler.TryReadback(DeviceContext);
	const TArray<FVisibleGridCell>& Cells = Data.VisibleGridCells;
	CellCount = static_cast<uint32>(Cells.Num());
	if (Cells.IsEmpty())
	{
		State.Culler.Release();
		SubmissionStats.SetHZBCounts(static_cast<uint32>(Data.Primitives.Num()), CellCount,
			OccludedCellCount, OccludedPrimitiveCount, true, bDispatched);
		return;
	}

	// 이전 읽기가 끝난 경우에만 단일 staging 버퍼에 새 요청을 보낸다.
	if (!State.Culler.IsReadbackPending())
	{
		const TArray<FHZBCellData> GPUCells = BuildHZBCellData(Cells);
		if (!State.Culler.UploadCells(D3DDevice, DeviceContext, GPUCells))
		{
			State.Culler.Release();
			SubmissionStats.SetHZBCounts(static_cast<uint32>(Data.Primitives.Num()), CellCount,
				OccludedCellCount, OccludedPrimitiveCount, true, bDispatched);
			return;
		}

		FHZBCullConstants Constants{};
		Constants.ViewProjection = Data.View.ViewProjection;
		Constants.CellCount = State.Culler.GetCellCount();
		Constants.ViewportWidth = Data.View.Viewport.Width;
		Constants.ViewportHeight = Data.View.Viewport.Height;
		Constants.HZBMipCount = HZB.MipCount;
		Constants.ViewportTopLeftX = Data.View.Viewport.TopLeftX;
		Constants.ViewportTopLeftY = Data.View.Viewport.TopLeftY;
		DeviceContext->UpdateSubresource(HZBCullConstantBuffer.Get(), 0, nullptr, &Constants, 0, 0);

		ID3D11Buffer* ConstantBuffer = HZBCullConstantBuffer.Get();
		ID3D11ShaderResourceView* SRVs[] = { State.Culler.GetCellSRV(), HZB.Texture };
		ID3D11UnorderedAccessView* UAV = State.Culler.GetVisibilityUAV();
		DeviceContext->CSSetShader(Shader, nullptr, 0);
		DeviceContext->CSSetConstantBuffers(1, 1, &ConstantBuffer);
		DeviceContext->CSSetShaderResources(1, 2, SRVs);
		DeviceContext->CSSetUnorderedAccessViews(1, 1, &UAV, nullptr);
		DeviceContext->Dispatch((Constants.CellCount + 63) / 64, 1, 1);
		bDispatched = true;

		// 다음 패스와 Hi-Z 생성에서 자원을 다시 바인딩할 수 있도록 해제한다.
		ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* NullUAV = nullptr;
		ID3D11Buffer* NullBuffer = nullptr;
		DeviceContext->CSSetShaderResources(1, 2, NullSRVs);
		DeviceContext->CSSetUnorderedAccessViews(1, 1, &NullUAV, nullptr);
		DeviceContext->CSSetConstantBuffers(1, 1, &NullBuffer);
		DeviceContext->CSSetShader(nullptr, nullptr, 0);
		TArray<uint64> CellKeys;
		CellKeys.Reserve(Cells.Num());
		for (const FVisibleGridCell& Cell : Cells)
		{
			CellKeys.Add(Cell.Key);
		}
		State.Culler.QueueReadback(DeviceContext, CellKeys);
	}

	// 선택 객체 등 셀에서 제외한 요청은 기본 가시성을 그대로 유지한다.
	for (const FVisibleGridCell& Cell : Cells)
	{
		if (State.Culler.IsVisibleLastFrame(Cell.Key)) continue;
		++OccludedCellCount;
		for (uint32 ItemIndex : Cell.PrimitiveIndices)
		{
			PrimitiveVisibility[ItemIndex] = 0;
			++OccludedPrimitiveCount;
		}
	}
	SubmissionStats.SetHZBCounts(static_cast<uint32>(Data.Primitives.Num()), CellCount,
		OccludedCellCount, OccludedPrimitiveCount, true, bDispatched);
}

// 해당 View의 컬링 결과를 반영한 뒤 기존 패스 순서로 렌더링한다.
void FViewRenderer::RenderView(
	const FViewRenderData& Data, const FHZBViewInput& HZB)
{
	if (!DeviceContext) return;

	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> Context1;
	if (FAILED(DeviceContext->QueryInterface(IID_PPV_ARGS(&Context1)))) return;

	SetViewportAndScissor(Data.View.Viewport);
	LineBatcher.Clear();
	LineBatcher.AddRequest(Data.Lines);
	CBRingBuffer.Reset();
	CBManager.Clear();

	// 수집 데이터의 ObjectIndex와 배열 위치는 그대로 유지한다.
	PreparePrimitiveVisibility(Data, HZB);
	PassDrawBuilder.BuildPassDraws(
		Data, &PipelineStateCache, PassDraws, PrimitiveVisibility);
	FOpaqueDrawSorter::SortOpaqueDraws(PassDraws.OpaqueDraws, OpaqueSortScratch);

	CBRingBuffer.BeginFrameMap(Context1.Get());
	CBManager.UploadObjectConstants(Context1.Get(), &CBRingBuffer,
		Data, PassDrawBuilder.GetReferenceObjectIndices());
	CBManager.UploadMaterialConstants(Context1.Get(), &CBRingBuffer,
		PassDrawBuilder.GetReferencedMaterials());
	CBRingBuffer.EndFrameMap(Context1.Get());

	// 첫 패스를 실행하기 전에 현재 View의 상수를 반영한다.
	UpdateTransformConstantBuffer(FMatrix::Identity, Data.View.ViewProjection);
	ID3D11Buffer* ViewCB = TransformConstantBuffer.Get();

	PassExecutor.ExecutePass(Context1.Get(), PassDraws.OpaqueDraws,
		PipelineStateCache, CBManager, ViewCB, SubmissionStats);

	RenderBatchLine(Data.View.ViewProjection);

	PassExecutor.ExecutePass(Context1.Get(), PassDraws.AdditiveDraws,
		PipelineStateCache, CBManager, ViewCB, SubmissionStats);
	PassExecutor.ExecutePass(Context1.Get(), PassDraws.OutlineDraws,
		PipelineStateCache, CBManager, ViewCB, SubmissionStats);
	PassExecutor.ExecutePass(Context1.Get(), PassDraws.GizmoDraws,
		PipelineStateCache, CBManager, ViewCB, SubmissionStats);

	if (!Data.TextItems.IsEmpty())
	{
		FFontAtlas* FontAtlas = GResourceManager::GetInstance()->GetDefaultFont();
		if (FontAtlas)
		{
			TArray<FVertexTexture> TextVerts =
				FTextMeshBuilder::Build(Data.TextItems, *FontAtlas);
			UpdateTextVertexBuffer(TextVerts);
			UpdateTransformConstantBuffer(FMatrix::Identity, Data.View.ViewProjection);

			const UINT TextVertexCount =
				static_cast<UINT>(TextVerts.Num()) < MaxTextVertices
				? static_cast<UINT>(TextVerts.Num()) : MaxTextVertices;
			RenderText(TextVertexCount / 4 * 6);
		}
	}

	UpdateTransformConstantBuffer(FMatrix::Identity, Data.View.ViewProjection);
}

void FViewRenderer::UpdateTransformConstantBuffer(const FMatrix& World, const FMatrix& VP)
{
	if (!DeviceContext || !TransformConstantBuffer)
	{
		return;
	}
	D3D11_MAPPED_SUBRESOURCE constantbufferMSR{};

	HRESULT hr = DeviceContext->Map(TransformConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR);
	if (SUCCEEDED(hr))
	{
		FConstants* constants = (FConstants*)constantbufferMSR.pData;
		if (constants)
		{
			constants->World = World;
			constants->ViewProjection = VP;
		}
		DeviceContext->Unmap(TransformConstantBuffer.Get(), 0);
	}
	else
	{
		UE_LOG("[FViewRenderer] Failed to Map TransformConstantBuffer. HRESULT: {}\n", hr);
	}
}

void FViewRenderer::UpdateMaterialConstants(const FPrimitiveRenderData& Data)
{
	if (!DeviceContext || !MaterialConstantBuffer || !Data.Material)	return;

	FTextureDrawConstants Constants{};
	
	Constants.DiffuseColor = Data.Material->DiffuseColor;
	Constants.AlphaCutoff = Data.Material->AlphaCutoff;
	Constants.UV.Scale = Data.Material->UVScale;
	Constants.UV.Offset = Data.Material->UVOffset;

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	HRESULT hr = DeviceContext->Map(MaterialConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);

	if (FAILED(hr)) return;

	memcpy(Mapped.pData, &Constants, sizeof(FTextureDrawConstants));
	DeviceContext->Unmap(MaterialConstantBuffer.Get(), 0);
}

void FViewRenderer::RenderOutline(const FPrimitiveRenderData& Data)
{
	// 두 레이아웃 모두 POSITION(0), COLOR(12) 배치라 VS_Highlight와 호환됨
	BindShader(*HighlightShader);
	BindPrimitiveBuffers(Data);

	// 안쪽은 스텐실이 가려주므로 컬링 불필요 (Plane처럼 한 면짜리도 처리)
	DeviceContext->RSSetState(CullNoneRasterizerState);

	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	DeviceContext->OMSetDepthStencilState(OutlineDepthStencilState, 1);

	DeviceContext->DrawIndexed(Data.Geometry.IndexCount, Data.Geometry.FirstIndex, Data.Geometry.BaseVertex);

	DeviceContext->OMSetDepthStencilState(DefaultDepthStencilState, 0);
}

void FViewRenderer::RenderHighlight(const FPrimitiveRenderData& Data)
{
	BindShader(*HighlightShader);
	BindPrimitiveBuffers(Data);

	DeviceContext->RSSetState(CullFrontRasterizerState);

	DeviceContext->OMSetDepthStencilState(HighlightDepthStencilState, 0);

	DeviceContext->DrawIndexed(Data.Geometry.IndexCount, Data.Geometry.FirstIndex, Data.Geometry.BaseVertex);
}

void FViewRenderer::RenderGizmo(const FPrimitiveRenderData& Data)
{
	BindShader(*SimpleShader);
	BindPrimitiveBuffers(Data);

	DeviceContext->RSSetState(DefaultRasterizerState);

	DeviceContext->OMSetDepthStencilState(GizmoDepthStencilState, 0);
	// BindMaterial(Data.Material); 

	DeviceContext->DrawIndexed(Data.Geometry.IndexCount, Data.Geometry.FirstIndex, Data.Geometry.BaseVertex);
}

void FViewRenderer::RenderBatchLine(const FMatrix& ViewProj)
{
	const UINT VertexCount = LineBatcher.GetVertexCount();
	const UINT IndexCount = LineBatcher.GetIndexCount();

	if (VertexCount == 0 || IndexCount == 0)
	{
		return;
	}

	if (!LineBatcher.Build())
	{
		LineBatcher.Clear();
		return;
	}

	UpdateTransformConstantBuffer(FMatrix::Identity, ViewProj);

	ID3D11Buffer* VB = LineBatcher.GetVertexBuffer();
	ID3D11Buffer* IB = LineBatcher.GetIndexBuffer();
	UINT Stride = sizeof(FVertexSimple);
	UINT Offset = 0;

	BindShader(*BatchLineShader);
	DeviceContext->IASetVertexBuffers(0, 1, &VB, &Stride, &Offset);
	DeviceContext->IASetIndexBuffer(IB, DXGI_FORMAT_R32_UINT, 0);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	DeviceContext->VSSetConstantBuffers(0, 1, TransformConstantBuffer.GetAddressOf());

	DeviceContext->RSSetState(DefaultRasterizerState);

	float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	UINT SampleMask = 0xffffffff;
	DeviceContext->OMSetBlendState(AlphaBlendState, BlendFactor, SampleMask);

	DeviceContext->OMSetDepthStencilState(DefaultDepthStencilState, 0);

	DeviceContext->DrawIndexed(IndexCount, 0, 0);
	SubmissionStats.RecordIndexedDraw(IndexCount, D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// 나중에 같은 데이터로 여러번 그리려면 Clear() 분리가 필요할 수 있음
	LineBatcher.Clear();
}

void FViewRenderer::BindShader(const FShaderResource& Shader)
{
	DeviceContext->IASetInputLayout(Shader.InputLayout.Get());
	DeviceContext->VSSetShader(Shader.VertexShader.Get(), nullptr, 0);
	DeviceContext->PSSetShader(Shader.PixelShader.Get(), nullptr, 0);
}

void FViewRenderer::BindPrimitiveBuffers(const FPrimitiveRenderData& Data)
{
	if (Data.Geometry.MeshPageId == InvalidRenderId)	return;

	const FMeshPageBinding PageBinding = GResourceManager::GetInstance()->GetMeshPageBinding(Data.Geometry.MeshPageId);

	if (!PageBinding.VertexBuffer || !PageBinding.IndexBuffer)	return;

	UINT Offset = 0;

	DeviceContext->IASetVertexBuffers(0, 1, &PageBinding.VertexBuffer, &PageBinding.Stride, &Offset);
	DeviceContext->IASetIndexBuffer(PageBinding.IndexBuffer, PageBinding.IndexFormat, 0);
	DeviceContext->IASetPrimitiveTopology(Data.Topology);
	DeviceContext->VSSetConstantBuffers(0, 1, TransformConstantBuffer.GetAddressOf());
}

bool FViewRenderer::BindMaterial(const FMaterial& Material)
{
	const FShaderResource* Shader = Material.Shader;

	if (!Shader ||!Shader->VertexShader ||!Shader->PixelShader ||!Shader->InputLayout)
	{
		return false;
	}

	BindShader(*Shader);

	DeviceContext->PSSetShaderResources(0, 1, &Material.SRV);

	DeviceContext->PSSetSamplers(0, 1, &Material.Sampler);

	const bool bAdditive = Material.BlendMode == EPrimitiveBlendMode::Additive;

	DeviceContext->OMSetBlendState(bAdditive ? AdditiveBlendState : nullptr, nullptr, 0xffffffff);

	DeviceContext->VSSetConstantBuffers(1, 1, MaterialConstantBuffer.GetAddressOf());

	DeviceContext->PSSetConstantBuffers(1, 1, MaterialConstantBuffer.GetAddressOf());

	return true;
}

void FViewRenderer::SetViewportAndScissor(const D3D11_VIEWPORT& Viewport)
{
	GContext::GetInstance()->SetViewportAndScissor(Viewport);
}
