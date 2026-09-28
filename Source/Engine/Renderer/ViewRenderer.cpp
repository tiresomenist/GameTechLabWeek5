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
}

void FViewRenderer::Shutdown()
{
	LineBatcher.Clear();
	LineBatcher.Release();
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

	D3D11_BUFFER_DESC materialConstantBufferDesc = {};
	materialConstantBufferDesc.ByteWidth = (sizeof(FTextureDrawConstants) + 0xf) & 0xfffffff0;
	materialConstantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	materialConstantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	materialConstantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	CheckHR(D3DDevice->CreateBuffer(&materialConstantBufferDesc, nullptr, MaterialConstantBuffer.GetAddressOf()));
}

void FViewRenderer::ReleaseConstantBuffer()
{
	TransformConstantBuffer.Reset();
	GridConstantBuffer.Reset();
	MaterialConstantBuffer.Reset();
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
		OutlineDepthStencilState
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

void FViewRenderer::RenderView(const FViewRenderData& Data)
{
	DeviceContext->RSSetState(DefaultRasterizerState);
	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	DeviceContext->OMSetDepthStencilState(DefaultDepthStencilState, 0);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	DeviceContext->PSSetShaderResources(0, 1, &NullSRV);

	LineBatcher.Clear();

	TArray<const FPrimitiveRenderData*> AdditiveRenderList;
	TArray<const FPrimitiveRenderData*> OutlineRenderList;

	for (const FPrimitiveRenderData& Item : Data.Primitives)
	{
		if (Item.Geometry.IndexCount == 0) 
			continue;

		if (!Item.Material) 
			continue;

		if (Item.Material->BlendMode == EPrimitiveBlendMode::Additive) 
			AdditiveRenderList.Add(&Item);

		if (Item.ObjectIndex >= static_cast<uint32>(Data.Objects.Num())) 
			continue;

		UpdateTransformConstantBuffer(Data.Objects[Item.ObjectIndex].World, Data.View.ViewProjection);

		const bool bSelected = (Item.Flags & Primitive_Selected) != 0;

		const bool bAllowOutline = (Item.Flags & Primitive_AllowOutline) != 0;

		const bool bOutline = bSelected && bAllowOutline && Data.View.ViewMode != EViewModeIndex::VMI_Wireframe;

		RenderPrimitive(Item, Data.View.ViewMode, bOutline);

		if (bOutline) {
			OutlineRenderList.Add(&Item);
		}
	}
	
	// 통합 데이터를 GPU에 업로드하고 배치 렌더링함
	RenderBatchLine(Data.View.ViewProjection);

	for (const FPrimitiveRenderData* Item : AdditiveRenderList)
	{
		if (!Item)	continue;

		if (Item->ObjectIndex >= static_cast<uint32>(Data.Objects.Num())) continue;

		UpdateTransformConstantBuffer(Data.Objects[Item->ObjectIndex].World, Data.View.ViewProjection);

		RenderPrimitive(*Item, Data.View.ViewMode);
	}

	// 외곽선: 모든 씬 오브젝트 이후, 기즈모 이전에 그림 (깊이 무시라 뒤에 그려진 물체에 덮이지 않게)
	for (const FPrimitiveRenderData* Item : OutlineRenderList)
	{
		if (!Item)	continue;

		if (Item->ObjectIndex >= static_cast<uint32>(Data.Objects.Num())) continue;

		UpdateTransformConstantBuffer(Data.Objects[Item->ObjectIndex].World, Data.View.ViewProjection);

		RenderOutline(*Item);
	}

	// Render Gizmo
	for (const FPrimitiveRenderData& Item : Data.Gizmos) {
		if (Item.Geometry.IndexCount == 0)	continue;

		if (!Item.Material)	continue;

		if (Item.ObjectIndex >= static_cast<uint32>(Data.Objects.Num()))	continue;
		
		UpdateTransformConstantBuffer(Data.Objects[Item.ObjectIndex].World, Data.View.ViewProjection);

		const bool bSelected = (Item.Flags & Primitive_Selected) != 0;

		if (bSelected) {
			RenderHighlight(Item);
		}

		RenderGizmo(Item);
	}

	//Text
	if (Data.TextItems.Num() > 0) {
		FFontAtlas* FontAtlas = GResourceManager::GetInstance()->GetDefaultFont();

		if (FontAtlas) {
			TArray<FVertexTexture> TextVerts = FTextMeshBuilder::Build(Data.TextItems, *FontAtlas);

			UpdateTextVertexBuffer(TextVerts);

			UpdateTransformConstantBuffer(FMatrix::Identity, Data.View.ViewProjection);

			const UINT TextVertexCount = (static_cast<UINT>(TextVerts.Num()) < MaxTextVertices) ?
				static_cast<UINT>(TextVerts.Num()) :
				MaxTextVertices;

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
