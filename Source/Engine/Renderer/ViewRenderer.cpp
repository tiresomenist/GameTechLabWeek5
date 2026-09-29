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
#include "Engine/Component/WidgetComponent.h"
#include "Engine/Component/Primitive/TextComponent.h"
#include "Engine/Renderer/Frustum.h"
#include <limits>

namespace
{
	// 이전 깊이를 재사용할 수 있도록 카메라와 출력 영역이 같은지 확인한다.
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

	uint64 MakeOcclusionCellKey(int32 X, int32 Y, int32 Z)
	{
		constexpr uint64 Mask = (1ull << 21) - 1;
		return ((static_cast<uint64>(X) & Mask) << 42) | ((static_cast<uint64>(Y) & Mask) << 21) 
			| (static_cast<uint64>(Z) & Mask);
	}

	TArray<FOcclusionCell> BuildOcclusionCells(const FViewRenderData& Data)
	{
		constexpr float CellSize = 8.0f;
		TArray<FOcclusionCell> Cells;
		TMap<uint64, int32> CellIndices;
		for (int32 Index = 0; Index < Data.Primitives.Num(); ++Index)
		{
			const FPrimitiveRenderData& Item = Data.Primitives[Index];
			if (!Item.Material || Item.ObjectIndex >= Data.Objects.Num()) continue;

			const FRenderObjectData& Object = Data.Objects[Item.ObjectIndex];
			if (!Object.bHasWorldBounds) continue;
			if (Item.Material->BlendMode == EPrimitiveBlendMode::Additive) continue;

			// 선택 객체는 기존 컬링 코드처럼 가려져도 표시 경로를 유지한다.
			if ((Item.Flags & Primitive_Selected) != 0) continue;

			const FBoundingBox& WorldBounds = Object.WorldBounds;

			const FVector Center = (WorldBounds.Min + WorldBounds.Max) * 0.5f;
			const int32 CellX = static_cast<int32>(std::floor(Center.X / CellSize));
			const int32 CellY = static_cast<int32>(std::floor(Center.Y / CellSize));
			const int32 CellZ = static_cast<int32>(std::floor(Center.Z / CellSize));
			const uint64 CellKey = MakeOcclusionCellKey(CellX, CellY, CellZ);
			int32* CellIndex = CellIndices.Find(CellKey);

			if (CellIndex == nullptr)
			{
				FOcclusionCell NewCell{};
				NewCell.Key = CellKey;
				NewCell.Bounds = WorldBounds;
				Cells.Add(std::move(NewCell));
				const int32 NewIndex = Cells.Num() - 1;
				CellIndices.Add(CellKey, NewIndex);
				CellIndex = CellIndices.Find(CellKey);
			}

			FOcclusionCell& Cell = Cells[*CellIndex];
			Cell.Bounds.Min.X = std::min(Cell.Bounds.Min.X, WorldBounds.Min.X);
			Cell.Bounds.Min.Y = std::min(Cell.Bounds.Min.Y, WorldBounds.Min.Y);
			Cell.Bounds.Min.Z = std::min(Cell.Bounds.Min.Z, WorldBounds.Min.Z);
			Cell.Bounds.Max.X = std::max(Cell.Bounds.Max.X, WorldBounds.Max.X);
			Cell.Bounds.Max.Y = std::max(Cell.Bounds.Max.Y, WorldBounds.Max.Y);
			Cell.Bounds.Max.Z = std::max(Cell.Bounds.Max.Z, WorldBounds.Max.Z);
			Cell.ItemIndices.Add(static_cast<uint32>(Index));
		}
		return Cells;
	}

	// GPU용 변환 배열 생성
	TArray<FHZBCellData> BuildHZBCellData(const TArray<FOcclusionCell>& OcclusionCells)
	{
		TArray<FHZBCellData> Result;
		Result.Reserve(OcclusionCells.Num());
		for (const FOcclusionCell& Cell : OcclusionCells)
		{
			FHZBCellData Data{};
			Data.BoundsMin = FVector4(Cell.Bounds.Min, 1.0f);
			Data.BoundsMax = FVector4(Cell.Bounds.Max, 1.0f);
			Result.Add(Data);
		}
		return Result;
	}
}
//void FViewRenderer::PreparePrimitiveVisibility(
//	const FViewRenderData& Data, const FHZBViewInput& HZB)
//{
//	PrimitiveVisibility.SetNum(Data.Primitives.Num());
//	for (uint32& Visible : PrimitiveVisibility) Visible = 1;
//
//	FHZBViewState& State = HZBViewStates[HZB.ViewId];
//	const bool bCanUseHistory =
//		HZB.bValid && HZB.Texture && HZB.MipCount > 0 &&
//		State.bHasPreviousView &&
//		State.PreviousFrameIndex + 1 == HZB.FrameIndex &&
//		State.PreviousGeneration == HZB.Generation &&
//		State.PreviousScene == HZB.Scene &&
//		IsSameHZBView(State.PreviousView, Data.View);
//
//	State.PreviousView = Data.View;
//	State.PreviousScene = HZB.Scene;
//	State.PreviousFrameIndex = HZB.FrameIndex;
//	State.PreviousGeneration = HZB.Generation;
//	State.bHasPreviousView = true;
//
//	if (!bCanUseHistory)
//	{
//		// 카메라 변경 전의 대기 결과까지 버리고 이번 View는 모두 표시한다.
//		State.Culler.Release();
//		return;
//	}
//
//	GResourceManager& Resources = *GResourceManager::GetInstance();
//	ID3D11ComputeShader* Shader = Resources.GetComputeShader(FName("HZB.CullCells"));
//	if (!Shader || !HZBCullConstantBuffer)
//	{
//		State.Culler.Release();
//		return;
//	}
//
//	State.Culler.TryReadback(DeviceContext);
//	const TArray<FOcclusionCell> Cells = BuildOcclusionCells(Data);
//
//	// 단일 staging 버퍼의 이전 요청이 끝난 경우에만 다음 요청을 제출한다.
//	if (!State.Culler.IsReadbackPending() && !Cells.IsEmpty())
//	{
//		const TArray<FHZBCellData> GPUCells = BuildHZBCellData(Cells);
//		if (!State.Culler.UploadCells(D3DDevice, DeviceContext, GPUCells))
//		{
//			State.Culler.Release();
//			return;
//		}
//
//		FHZBCullConstants Constants{};
//		Constants.ViewProjection = Data.View.ViewProjection;
//		Constants.CellCount = State.Culler.GetCellCount();
//		Constants.ViewportWidth = Data.View.Viewport.Width;
//		Constants.ViewportHeight = Data.View.Viewport.Height;
//		Constants.HZBMipCount = HZB.MipCount;
//		Constants.ViewportTopLeftX = Data.View.Viewport.TopLeftX;
//		Constants.ViewportTopLeftY = Data.View.Viewport.TopLeftY;
//		DeviceContext->UpdateSubresource(
//			HZBCullConstantBuffer.Get(), 0, nullptr, &Constants, 0, 0);
//
//		ID3D11Buffer* ConstantBuffer = HZBCullConstantBuffer.Get();
//		ID3D11ShaderResourceView* SRVs[] = {
//			State.Culler.GetCellSRV(), HZB.Texture
//		};
//		ID3D11UnorderedAccessView* UAV = State.Culler.GetVisibilityUAV();
//
//		DeviceContext->CSSetShader(Shader, nullptr, 0);
//		DeviceContext->CSSetConstantBuffers(1, 1, &ConstantBuffer);
//		DeviceContext->CSSetShaderResources(1, 2, SRVs);
//		DeviceContext->CSSetUnorderedAccessViews(1, 1, &UAV, nullptr);
//		DeviceContext->Dispatch((Constants.CellCount + 63) / 64, 1, 1);
//
//		// 다음 깊이 생성과 다른 패스가 자원을 다시 사용할 수 있도록 해제한다.
//		ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
//		ID3D11UnorderedAccessView* NullUAV = nullptr;
//		ID3D11Buffer* NullBuffer = nullptr;
//		DeviceContext->CSSetShaderResources(1, 2, NullSRVs);
//		DeviceContext->CSSetUnorderedAccessViews(1, 1, &NullUAV, nullptr);
//		DeviceContext->CSSetConstantBuffers(1, 1, &NullBuffer);
//		DeviceContext->CSSetShader(nullptr, nullptr, 0);
//
//		State.Culler.QueueReadback(DeviceContext, Cells);
//	}
//
//	// 셀에 포함되지 않은 선택 객체·Additive·Bounds 없는 객체는 계속 표시한다.
//	for (const FOcclusionCell& Cell : Cells)
//	{
//		if (State.Culler.IsVisibleLastFrame(Cell.Key)) continue;
//		for (uint32 ItemIndex : Cell.ItemIndices)
//		{
//			PrimitiveVisibility[ItemIndex] = 0;
//		}
//	}
//}
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
	OcclusionCuller.CreateProxyMesh(D3DDevice);
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
	OcclusionBlendState = Resources.GetBlendState(FName("Blend.Occlusion"));
	if (!AlphaBlendState || !AdditiveBlendState || !OcclusionBlendState)
	{
		throw std::runtime_error("Required blend states are missing");
	}
}

void FViewRenderer::ReleaseAlphaBlendState()
{
	// 공유 자원은 ResourceManager가 해제한다.
	AlphaBlendState = nullptr;
	AdditiveBlendState = nullptr;
	OcclusionBlendState = nullptr;
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
	OcclusionDepthStencilState = Resources.GetDepthStencilState(FName("Depth.Occlusion"));
	const TArray<ID3D11DepthStencilState*> RequiredStates
	{
		DefaultDepthStencilState,
		GizmoDepthStencilState,
		HighlightDepthStencilState,
		TranslucentDepthStencilState,
		TextDepthStencilState,
		StencilWriteDepthStencilState,
		OutlineDepthStencilState,
		OcclusionDepthStencilState
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
	OcclusionDepthStencilState = nullptr;
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
}

void FViewRenderer::ReleaseTextResources()
{
	// 공유 자원은 참조만 비운다.
	TextShader = nullptr;
	FontSamplerState = nullptr;

	TextVertexBuffer.Reset();
	TextIndexBuffer.Reset();
	TextVertexCapacity = 0;
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
		// 첫 프레임이나 카메라 변경 이전의 결과는 사용하지 않는다.
		State.Culler.Release();
		return;
	}

	GResourceManager& Resources = *GResourceManager::GetInstance();
	ID3D11ComputeShader* Shader = Resources.GetComputeShader(FName("HZB.CullCells"));
	if (!Shader || !HZBCullConstantBuffer)
	{
		State.Culler.Release();
		return;
	}

	State.Culler.TryReadback(DeviceContext);
	const TArray<FOcclusionCell> Cells = BuildOcclusionCells(Data);
	if (Cells.IsEmpty())
	{
		State.Culler.Release();
		return;
	}

	// 이전 읽기가 끝난 경우에만 단일 staging 버퍼에 새 요청을 보낸다.
	if (!State.Culler.IsReadbackPending())
	{
		const TArray<FHZBCellData> GPUCells = BuildHZBCellData(Cells);
		if (!State.Culler.UploadCells(D3DDevice, DeviceContext, GPUCells))
		{
			State.Culler.Release();
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

		// 다음 패스와 Hi-Z 생성에서 자원을 다시 바인딩할 수 있도록 해제한다.
		ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* NullUAV = nullptr;
		ID3D11Buffer* NullBuffer = nullptr;
		DeviceContext->CSSetShaderResources(1, 2, NullSRVs);
		DeviceContext->CSSetUnorderedAccessViews(1, 1, &NullUAV, nullptr);
		DeviceContext->CSSetConstantBuffers(1, 1, &NullBuffer);
		DeviceContext->CSSetShader(nullptr, nullptr, 0);
		State.Culler.QueueReadback(DeviceContext, Cells);
	}

	// 선택 객체 등 셀에서 제외한 요청은 기본 가시성을 그대로 유지한다.
	for (const FOcclusionCell& Cell : Cells)
	{
		if (State.Culler.IsVisibleLastFrame(Cell.Key)) continue;
		for (uint32 ItemIndex : Cell.ItemIndices)
		{
			PrimitiveVisibility[ItemIndex] = 0;
		}
	}
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

	RenderVisibleText(Data);

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

bool FViewRenderer::RenderOcclusionProxy(const FBoundingBox& WorldBounds, const FMatrix& ViewProjection)
{
	ID3D11Buffer* ProxyVertexBuffer = OcclusionCuller.GetProxyVertexBuffer();
	ID3D11Buffer* ProxyIndexBuffer = OcclusionCuller.GetProxyIndexBuffer();

	if (!ProxyVertexBuffer || !ProxyIndexBuffer)
	{
		return false;
	}

	const FVector WorldCenter = (WorldBounds.Min + WorldBounds.Max) * 0.5f;
	const FVector WorldExtent = (WorldBounds.Max - WorldBounds.Min) * 0.5f;
	if (WorldExtent.X <= EPSILON || WorldExtent.Y <= EPSILON || WorldExtent.Z <= EPSILON)
	{
		return false;
	}

	const FMatrix ProxyWorld = FMatrix::MakeScaleMatrix(WorldExtent) 
		* FMatrix::MakeTranslationMatrix(WorldCenter);

	UpdateTransformConstantBuffer(ProxyWorld, ViewProjection);
	BindShader(*SimpleShader);

	const UINT Stride = sizeof(FVertexSimple);
	const UINT Offset = 0;

	DeviceContext->IASetVertexBuffers(0, 1, &ProxyVertexBuffer, &Stride, &Offset);
	DeviceContext->IASetIndexBuffer(ProxyIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DeviceContext->VSSetConstantBuffers(0, 1, TransformConstantBuffer.GetAddressOf());

	DeviceContext->RSSetState(CullNoneRasterizerState);
	DeviceContext->OMSetBlendState(OcclusionBlendState, nullptr, 0xffffffff);
	DeviceContext->OMSetDepthStencilState(OcclusionDepthStencilState, 0);
	DeviceContext->PSSetShader(nullptr, nullptr, 0);
	DeviceContext->DrawIndexed(36, 0, 0);
	
	return true;
}


// 용량 부족 시에만 버퍼를 확장하고, 두 버퍼 생성 성공 후 기존 자원을 교체한다.
bool FViewRenderer::EnsureTextCapacity(UINT RequiredVertices)
{
	if (RequiredVertices == 0) return true;
	if (TextVertexBuffer && TextIndexBuffer && RequiredVertices <= TextVertexCapacity) return true;

	constexpr UINT MaxVertices =((std::numeric_limits<UINT>::max)() / sizeof(FVertexTexture) / 4) * 4;
	if (RequiredVertices > MaxVertices || RequiredVertices % 4 != 0) return false;

	UINT NewCapacity = TextVertexCapacity ? TextVertexCapacity : 8192;
	while (NewCapacity < RequiredVertices)
	{
		NewCapacity = static_cast<UINT>((std::min)(static_cast<uint64>(MaxVertices),static_cast<uint64>(NewCapacity) * 2));
	}

	Microsoft::WRL::ComPtr<ID3D11Buffer> NewVB;
	Microsoft::WRL::ComPtr<ID3D11Buffer> NewIB;

	D3D11_BUFFER_DESC VBDesc{};
	VBDesc.ByteWidth = NewCapacity * sizeof(FVertexTexture);
	VBDesc.Usage = D3D11_USAGE_DYNAMIC;
	VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	VBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(D3DDevice->CreateBuffer(&VBDesc, nullptr, NewVB.GetAddressOf()))) return false;

	// 글자 인덱스는 내용과 무관하므로 확장할 때만 만든다.
	TArray<uint32> Indices;
	Indices.Reserve(static_cast<size_t>(NewCapacity / 4) * 6);
	for (UINT Base = 0; Base < NewCapacity; Base += 4)
	{
		Indices.Add(Base);
		Indices.Add(Base + 1);
		Indices.Add(Base + 2);
		Indices.Add(Base);
		Indices.Add(Base + 2);
		Indices.Add(Base + 3);
	}

	D3D11_BUFFER_DESC IBDesc{};
	IBDesc.ByteWidth = static_cast<UINT>(Indices.Num() * sizeof(uint32));
	IBDesc.Usage = D3D11_USAGE_IMMUTABLE;
	IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA IBData{};
	IBData.pSysMem = Indices.GetData();
	if (FAILED(D3DDevice->CreateBuffer(&IBDesc, &IBData, NewIB.GetAddressOf()))) return false;

	TextVertexBuffer = std::move(NewVB);
	TextIndexBuffer = std::move(NewIB);
	TextVertexCapacity = NewCapacity;
	return true;
}

// 고정 최대치로 자르지 않고 생성된 정점 전체를 업로드한다.
bool FViewRenderer::UpdateTextVertexBuffer(const TArray<FVertexTexture>& Vertices)
{
	if (Vertices.IsEmpty()) return true;
	if (!EnsureTextCapacity(static_cast<UINT>(Vertices.Num()))) return false;

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(DeviceContext->Map(TextVertexBuffer.Get(), 0,D3D11_MAP_WRITE_DISCARD, 0, &Mapped))) return false;

	memcpy(Mapped.pData, Vertices.GetData(),static_cast<size_t>(Vertices.Num()) * sizeof(FVertexTexture));
	DeviceContext->Unmap(TextVertexBuffer.Get(), 0);
	return true;
}

// 메시에서 살아 있는 객체의 UUID와 화면 안의 일반 텍스트만 출력한다.
void FViewRenderer::RenderVisibleText(const FViewRenderData& Data)
{
	if (!Data.TextCamera || Data.TextRequests.IsEmpty()) return;

	TextVertexScratch.Empty();
	TextObjectVisibility.SetNum(Data.Objects.Num());
	for (uint32& Visible : TextObjectVisibility) Visible = 0;

	// 여러 섹션 중 하나라도 살아 있으면 해당 객체의 UUID를 표시한다.
	for (int32 Index = 0; Index < Data.Primitives.Num(); ++Index)
	{
		const uint32 ObjectIndex = Data.Primitives[Index].ObjectIndex;
		if (PrimitiveVisibility[Index] != 0 && ObjectIndex < static_cast<uint32>(TextObjectVisibility.Num()))
		{
			TextObjectVisibility[ObjectIndex] = 1;
		}
	}

	const FFrustum Frustum = FFrustum::FrustumFromViewProjection(Data.View.ViewProjection);

	for (const FTextDrawRequest& Request : Data.TextRequests)
	{
		if (Request.OwnerObjectIndex != InvalidRenderId)
		{
			if (Request.OwnerObjectIndex >=	static_cast<uint32>(TextObjectVisibility.Num()) ||
				TextObjectVisibility[Request.OwnerObjectIndex] == 0)
			{
				continue;
			}
		}

		// HZB로 탈락한 UUID는 문자열·앵커 계산도 수행하지 않는다.
		FWorldTextItem Item{};
		bool bBuilt = false;
		if (Request.Widget)
			bBuilt = Request.Widget->BuildTextItem(Data.TextCamera, Item);
		else if (Request.TextComponent)
			bBuilt = Request.TextComponent->BuildTextItem(Data.TextCamera, Item);

		if (!bBuilt || !Item.Layout) continue;

		// 라벨 자체가 화면 밖이면 월드 정점 생성과 업로드를 생략한다.
		const FBoundingBox WorldBounds = Item.Layout->LocalBounds.TransformBounds(Item.WorldMatrix);
		if (!Frustum.Intersects(WorldBounds)) continue;

		FTextMeshBuilder::AppendCached(TextVertexScratch, *Item.Layout, Item.WorldMatrix);
	}

	if (TextVertexScratch.IsEmpty()) return;
	if (!UpdateTextVertexBuffer(TextVertexScratch))
	{
		UE_LOG("[Text] 텍스트 버퍼 생성 또는 업로드 실패");
		return;
	}

	UpdateTransformConstantBuffer(FMatrix::Identity, Data.View.ViewProjection);
	RenderText(static_cast<UINT>(TextVertexScratch.Num() / 4) * 6);
}