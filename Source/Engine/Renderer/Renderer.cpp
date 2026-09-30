#include "pch.h"
#include "Engine/Util/DebugCpuStats.h"
#include "Renderer.h"
#include "Engine/Renderer/Device.h"
#include "Engine/Renderer/Context.h"
#include "Engine/Log.h"
#include "Editor/Editor.h"
#include "Editor/Window/EditorWindow.h"
#include "Engine/Resource/ResourceManager.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <stdexcept>
#include <utility>
#include <cstdlib>
#include <chrono>
#include <dxgi1_5.h>
#include "Engine/Renderer/RenderUtil.h"
#include "Engine/Component/CameraComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

using Microsoft::WRL::ComPtr;
namespace
{
	bool AppendLineRequest(FLineDrawRequest& Destination, const FLineDrawRequest& Request)
	{
		const size_t AddedVertices = static_cast<size_t>(Request.Vertices.Num());
		const size_t AddedIndices = static_cast<size_t>(Request.Indices.Num());

		if (AddedVertices == 0 && AddedIndices == 0)
		{
			return true;
		}

		if (AddedVertices == 0 || AddedIndices == 0 || AddedIndices % 2 != 0)
		{
			return false;
		}

		const size_t MaxVertices = (std::min)(
			static_cast<size_t>((std::numeric_limits<int>::max)()),
			static_cast<size_t>((std::numeric_limits<UINT>::max)()) /
			sizeof(FVertexSimple));

		const size_t MaxIndices = (std::min)(
			static_cast<size_t>((std::numeric_limits<int>::max)()),
			static_cast<size_t>((std::numeric_limits<UINT>::max)()) /
			sizeof(uint32));

		const size_t CurrentVertices = static_cast<size_t>(Destination.Vertices.Num());
		const size_t CurrentIndices = static_cast<size_t>(Destination.Indices.Num());

		if (CurrentVertices > MaxVertices || AddedVertices > MaxVertices - CurrentVertices ||
			CurrentIndices > MaxIndices || AddedIndices > MaxIndices - CurrentIndices)
		{
			return false;
		}

		for (uint32 Index : Request.Indices)
		{
			if (Index >= AddedVertices)
			{
				return false;
			}
		}

		for (const FVertexSimple& Vertex : Request.Vertices)
		{
			if (!std::isfinite(Vertex.x) ||
				!std::isfinite(Vertex.y) ||
				!std::isfinite(Vertex.z) ||
				!std::isfinite(Vertex.r) ||
				!std::isfinite(Vertex.g) ||
				!std::isfinite(Vertex.b) ||
				!std::isfinite(Vertex.a))
			{
				return false;
			}
		}

		try
		{
			Destination.Vertices.Reserve(CurrentVertices + AddedVertices);
			Destination.Indices.Reserve(CurrentIndices + AddedIndices);
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}

		const uint32 VertexBase = static_cast<uint32>(CurrentVertices);

		for (const FVertexSimple& Vertex : Request.Vertices)
		{
			Destination.Vertices.Add(Vertex);
		}

		for (uint32 Index : Request.Indices)
		{
			Destination.Indices.Add(VertexBase + Index);
		}

		return true;
	}
}


struct FHZBConstants
{
	uint32 SourceWidth = 0;
	uint32 SourceHeight = 0;
	UINT SourceMip = 0;
	uint32 Padding = 0;
};

void FRenderer::Create(HWND HWnd, GDevice* InDevice, uint32 Width, uint32 Height)
{
	InvalidateOcclusionHistory();
	GContext& Context = *GContext::GetInstance();
	if (Device) {
		throw std::logic_error("Renderer is Already initialized");
	}
	if (!HWnd || Width == 0 || Height == 0)
	{
		throw std::invalid_argument("Invalid renderer output parameters");
	}

	if (!InDevice || !InDevice->GetDevice() || !Context.IsInitialized()) {
		throw std::runtime_error("Renderer requires an initialized device");
	}
	
	bRenderReady = false;
	bGraphicsFailed = false;

	Device = InDevice;
	DeviceContext = Context.GetNative();
	D3DDevice = InDevice->GetDevice();
	if (!CreateSwapChain(HWnd, Width, Height) || !CreateFrameBuffer() || 
		!CreateDepthStencilBuffer(static_cast<uint32>(ViewportInfo.Width),static_cast<uint32>(ViewportInfo.Height)) ||
		!HierarchicalZBuffer.Create(D3DDevice, static_cast<uint32>(ViewportInfo.Width), static_cast<uint32>(ViewportInfo.Height)))
	{
		throw std::runtime_error("Failed to create renderer output");
	}

	if (!CreateHZBConstantBuffer())
	{
		throw std::runtime_error("Failed to create HZB constant buffer");
	}

	ViewRenderer.Create(D3DDevice, DeviceContext);
	GPUTimer.Initialize(D3DDevice);
	IMGUI_CHECKVERSION();
	if (!ImGui::CreateContext()) throw std::runtime_error("ImGui context failed");
	bImGuiContextCreated = true;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.Fonts->AddFontFromFileTTF("Assets/Fonts/Pretendard-Regular.ttf");

	ImGuiStyle& Style = ImGui::GetStyle();
	Style.FontSizeBase = 16.0f;
	Style.FontScaleMain = 1.0f;

	const float DPISCale = GetDpiForWindow(HWnd) / 96.0f;
	Style.FontScaleDpi = DPISCale;
	io.ConfigDpiScaleFonts = true;
	Style.ScaleAllSizes(DPISCale);

	Style.WindowMenuButtonPosition = ImGuiDir_None;

	// Setup Platform/Renderer backends
	bImGuiWin32Initialized = ImGui_ImplWin32_Init(HWnd);
	if (!bImGuiWin32Initialized) throw std::runtime_error("ImGui Win32 initialization failed");
	bImGuiDX11Initialized = ImGui_ImplDX11_Init(D3DDevice, DeviceContext);
	if (!bImGuiDX11Initialized) throw std::runtime_error("ImGui DX11 initialization failed");
	if (!ImGui_ImplDX11_CreateDeviceObjects())
		throw std::runtime_error("ImGui GPU resource creation failed");
	bRenderReady = true;
}

void FRenderer::Shutdown()
{
	InvalidateOcclusionHistory();
	bRenderReady = false;

	if (DeviceContext){	DeviceContext->ClearState();}

	ViewRenderer.Shutdown();
	GPUTimer.Release();

	if (bImGuiDX11Initialized){ImGui_ImplDX11_Shutdown();}

	if (bImGuiWin32Initialized){ImGui_ImplWin32_Shutdown();}

	if (bImGuiContextCreated){ImGui::DestroyContext();}

	bImGuiDX11Initialized = false;
	bImGuiWin32Initialized = false;
	bImGuiContextCreated = false;

	HZBConstantBuffer.Reset();
	HierarchicalZBuffer.Release();
	ReleaseDepthStencilBuffer();
	ReleaseFrameBuffer();
	SwapChain.Reset();
	bTearingSupported = false;
	ViewportInfo = {};

	DeviceContext = nullptr;
	D3DDevice = nullptr;
	Device = nullptr;
}

bool FRenderer::IsRenderReady() const
{
	return bRenderReady && !bGraphicsFailed;
}

const D3D11_VIEWPORT& FRenderer::GetViewport() const
{
	return ViewportInfo;
}

void FRenderer::SwapBuffer()
{
	if (!IsRenderReady() || !SwapChain.Get()) { return; }

	// 독점 전체화면에서는 티어링 Present 플래그를 사용할 수 없다.
	BOOL Fullscreen = FALSE;
	const UINT PresentFlags = bTearingSupported &&
		SUCCEEDED(SwapChain->GetFullscreenState(&Fullscreen, nullptr)) && !Fullscreen
		? DXGI_PRESENT_ALLOW_TEARING : 0;

	using Clock = std::chrono::high_resolution_clock;
	auto StartWait = Clock::now();

	HRESULT Result;
	{
		FScopedDebugCpuTime CpuTime(EDebugCpuStat::Present);
		Result = SwapChain->Present(0, PresentFlags);
	}

	auto EndWait = Clock::now();
	float CurWaitMs = std::chrono::duration<float, std::milli>(EndWait - StartWait).count();
	GPUWaitMs = (GPUWaitMs * 0.9f) + (CurWaitMs * 0.1f);

	if (FAILED(Result))
	{
		bRenderReady = false;
		bGraphicsFailed = true;
		PostQuitMessage(EXIT_FAILURE);
	}
}

void FRenderer::PrepareRTVDSV()
{
	GContext& Context = *GContext::GetInstance();

	SetViewportAndScissor(ViewportInfo);

	DeviceContext->ClearRenderTargetView(FrameBufferRTV.Get(), ClearColor);
	DeviceContext->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	Context.SetRenderTargets(FrameBufferRTV.Get(), DepthStencilView.Get());

	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
}

void FRenderer::BeginFrame()
{
	++HZBFrameIndex;
	ViewRenderer.BeginSubmissionFrame();
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	PrepareRTVDSV();
}

void FRenderer::EndFrame()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	GPUTimer.EndFrame(DeviceContext);

	SwapBuffer();

	GContext::GetInstance()->UnbindRenderTargets();
}

bool FRenderer::CreateSwapChain(HWND HWnd, uint32 Width, uint32 Height)
{
	if (!D3DDevice || !HWnd || Width == 0 || Height == 0)
	{
		return false;
	}

	ComPtr<IDXGIDevice> DxgiDevice;
	ComPtr<IDXGIAdapter> Adapter;
	ComPtr<IDXGIFactory> Factory;

	HRESULT Result = D3DDevice->QueryInterface(IID_PPV_ARGS(DxgiDevice.GetAddressOf()));

	if (FAILED(Result)){return false;}

	Result = DxgiDevice->GetAdapter(Adapter.GetAddressOf());
	if (FAILED(Result))
	{
		return false;
	}

	Result = Adapter->GetParent(IID_PPV_ARGS(Factory.GetAddressOf()));

	if (FAILED(Result))
	{
		return false;
	}

	bTearingSupported = false;
	ComPtr<IDXGIFactory5> Factory5;
	if (SUCCEEDED(Factory.As(&Factory5)))
	{
		BOOL AllowTearing = FALSE;
		bTearingSupported = SUCCEEDED(Factory5->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING, &AllowTearing, sizeof(AllowTearing))) && AllowTearing;
	}

	DXGI_SWAP_CHAIN_DESC Desc{};
	Desc.BufferDesc.Width = Width;
	Desc.BufferDesc.Height = Height;
	Desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	Desc.BufferCount = 2;
	Desc.OutputWindow = HWnd;
	Desc.Windowed = TRUE;
	Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	Desc.Flags = bTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain> NewSwapChain;

	Result = Factory->CreateSwapChain(D3DDevice, &Desc, NewSwapChain.GetAddressOf());

	if (FAILED(Result))
	{
		UE_LOG("[FRenderer] Failed to create swap chain. HRESULT: {}\n", Result);
		return false;
	}

	Result = NewSwapChain->GetDesc(&Desc);
	if (FAILED(Result))
	{
		return false;
	}

	SwapChain = std::move(NewSwapChain);

	ViewportInfo = {0.0f,0.0f,static_cast<float>(Desc.BufferDesc.Width),
		static_cast<float>(Desc.BufferDesc.Height),	0.0f,1.0f};

	return true;
}

bool FRenderer::CreateFrameBuffer()
{
	if (!D3DDevice || !SwapChain.Get())
	{
		return false;
	}

	ComPtr<ID3D11Texture2D> NewFrameBuffer;
	ComPtr<ID3D11RenderTargetView> NewRTV;

	HRESULT Result = SwapChain->GetBuffer(0, IID_PPV_ARGS(NewFrameBuffer.GetAddressOf()));

	if (FAILED(Result))
	{
		UE_LOG("[FRenderer] Failed to get back buffer from SwapChain. HRESULT: {}\n", Result);
		ReleaseFrameBuffer();
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC RTVDesc{};
	RTVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	Result = D3DDevice->CreateRenderTargetView(NewFrameBuffer.Get(), &RTVDesc, NewRTV.GetAddressOf());

	if (FAILED(Result))
	{
		UE_LOG("[FRenderer] Failed to create Render Target View. HRESULT: {}\n", Result);
		return false;
	}

	FrameBuffer = std::move(NewFrameBuffer);
	FrameBufferRTV = std::move(NewRTV);

	return true;
}

void FRenderer::ReleaseFrameBuffer()
{
	FrameBufferRTV.Reset();
	FrameBuffer.Reset();
}

bool FRenderer::CreateDepthStencilBuffer(uint32 Width, uint32 Height)
{
	if (!D3DDevice || Width == 0 || Height == 0){ return false; }

	ComPtr<ID3D11Texture2D> NewDepthBuffer;
	ComPtr<ID3D11DepthStencilView> NewDSV;
	ComPtr< ID3D11ShaderResourceView> NewSRV;

	D3D11_TEXTURE2D_DESC DepthStencilDesc{};
	DepthStencilDesc.Width = Width;
	DepthStencilDesc.Height = Height;
	DepthStencilDesc.MipLevels = 1;
	DepthStencilDesc.ArraySize = 1;
	DepthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	DepthStencilDesc.SampleDesc.Count = 1;
	DepthStencilDesc.SampleDesc.Quality = 0;
	DepthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	DepthStencilDesc.CPUAccessFlags = 0;
	DepthStencilDesc.MiscFlags = 0;

	HRESULT Result = D3DDevice->CreateTexture2D(&DepthStencilDesc, nullptr, NewDepthBuffer.GetAddressOf());

	if (FAILED(Result)){ return false; }

	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc{};
	DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;
	Result = D3DDevice->CreateDepthStencilView(NewDepthBuffer.Get(),&DSVDesc, NewDSV.GetAddressOf());
	if (FAILED(Result))
	{
		UE_LOG("[FRenderer] Failed to create depth stencil view. HRESULT: {}\n",Result);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;
	Result = D3DDevice->CreateShaderResourceView(NewDepthBuffer.Get(), &SRVDesc, NewSRV.GetAddressOf());
	if (FAILED(Result))
	{
		UE_LOG("[FRenderer] Failed to create shader resource view. HRESULT: {}\n", Result);
		return false;
	}

	DepthStencilBuffer = std::move(NewDepthBuffer);
	DepthStencilView = std::move(NewDSV);
	DepthStencilSRV = std::move(NewSRV);

	return true;
}

void FRenderer::ReleaseDepthStencilBuffer()
{
	DepthStencilView.Reset();
	DepthStencilBuffer.Reset();
	DepthStencilSRV.Reset();
}

void FRenderer::OnResize(uint32 Width, uint32 Height)
{
	InvalidateOcclusionHistory();
	bRenderReady = false;

	GContext& Context = *GContext::GetInstance();

	if (Width == 0 || Height == 0 || !D3DDevice || !Context.IsInitialized() || !SwapChain.Get() || bGraphicsFailed)
	{ return; }

	Context.UnbindRenderTargets();

	ReleaseDepthStencilBuffer();
	ReleaseFrameBuffer();

	Context.Flush();

	const UINT SwapChainFlags = bTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
	const HRESULT Result = SwapChain->ResizeBuffers(0,Width,Height,	DXGI_FORMAT_UNKNOWN, SwapChainFlags);

	if (FAILED(Result))
	{
		bGraphicsFailed = true;
		PostQuitMessage(EXIT_FAILURE);
		return;
	}

	ViewportInfo = {0.0f,0.0f,static_cast<float>(Width),static_cast<float>(Height),0.0f,1.0f};

	if (!CreateFrameBuffer() || !CreateDepthStencilBuffer(Width, Height)
		|| !HierarchicalZBuffer.Resize(D3DDevice, Width, Height))
	{
		HierarchicalZBuffer.Release();
		ReleaseFrameBuffer();
		ReleaseDepthStencilBuffer();

		bGraphicsFailed = true;
		PostQuitMessage(EXIT_FAILURE);
		return;
	}

	Context.SetRenderTargets(FrameBufferRTV.Get(),DepthStencilView.Get());

	Context.SetViewport(ViewportInfo);

	bRenderReady = true;
}

void FRenderer::RenderOneView(FEditor* Editor, UScene* Scene, const FRenderView& View)
{
	ViewData.Reset();

	if (!Editor || !Scene || !View.Camera ||View.Viewport.Width <= 0.0f || View.Viewport.Height <= 0.0f)
	{
		return;
	}

	UCameraComponent* Camera = View.Camera;

	Camera->SetAspectRatio(View.Viewport.Width / View.Viewport.Height);

	ViewData.View.ViewMatrix = Camera->GetViewMatrix();
	ViewData.View.ViewProjection = ViewData.View.ViewMatrix * Camera->GetProjectionMatrix();

	ViewData.View.Viewport = View.Viewport;
	ViewData.View.ViewMode = View.ViewSettings.ViewMode;

	const FFrustum Frustum = FFrustum::FrustumFromViewProjection(ViewData.View.ViewProjection);

	FHZBViewInput HZB{};
	HZB.Texture = HierarchicalZBuffer.GetSRV();
	HZB.Scene = Scene;
	HZB.MipCount = HierarchicalZBuffer.GetMipCount();
	HZB.ViewId = static_cast<uint32>(View.ViewType);
	HZB.FrameIndex = HZBFrameIndex;
	HZB.Generation = HZBGeneration;
	HZB.bValid = bHZBValid && View.ViewSettings.ViewMode != EViewModeIndex::VMI_Wireframe &&
		View.ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Primitives) &&
		View.Viewport.MinDepth == 0.0f && View.Viewport.MaxDepth == 1.0f;

	if (View.ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::Primitives))
	{
		RenderUtil::GatherGridCellCandidates(Scene, &Frustum, ViewData.GridCellCandidates);

		TArray<FGridCellCandidate> RenderGridCells;
		ViewRenderer.FilterGridCellCandidates(ViewData.View, HZB, ViewData.GridCellCandidates, RenderGridCells);
		ViewData.HZBRenderCellCount = static_cast<uint32>(RenderGridCells.Num());

		RenderUtil::GetRenderList(Editor, Scene, Camera, ViewData.Primitives, ViewData.Objects,
			RenderGridCells, &Frustum);
	}
	if (View.bDrawEditorGizmos)
	{
		ViewData.Gizmos = RenderUtil::GetGizmoList(Editor,Scene,Camera,View.Viewport,ViewData.Objects);
	}

	RenderUtil::GetTextRenderList(Scene, Camera,View.ViewSettings.ShowFlags.IsEnabled(EEngineShowFlag::UUID),ViewData);

	const FLineRequestConsumer Submit = [&](const FLineDrawRequest& Request)
		{
			if (!AppendLineRequest(ViewData.Lines, Request))
			{
				UE_LOG("[RenderUtil] 잘못되었거나 용량을 초과한 라인 요청");
			}
		};

	RenderUtil::SubmitLineDrawRequests(Editor,Scene,Camera,View.ViewSettings,Submit,View.ViewType);

	ViewRenderer.RenderView(ViewData, HZB);
	ViewRenderer.ReleaseViewReferences();
	ViewData.Reset();
}

// 단일 View -> 이제 더이상 다중 View를 호출하지 않음
void FRenderer::Render(float DeltaTime,FEditor* Editor,UScene* Scene)
{
	using Clock = std::chrono::high_resolution_clock;
	if (!IsRenderReady() || !Editor || !Scene) return;

	GPUTimer.BeginFrame(DeviceContext);
	auto StartDraw = Clock::now();

	BeginFrame();
	Editor->DrawMenu();

	// 메뉴에서 갱신한 설정과 출력 영역으로 렌더 뷰를 생성합니다.
	const TArray<FRenderView> Views = Editor->BuildRenderViews(ViewportInfo);
	for (const FRenderView& View : Views)
	{
		//TODO:ViewRenderer구현 후 RenderOneView(Editor, Scene, View); 로 교체해주면 됨.
		RenderOneView(Editor, Scene, View);
	}
	FinishHZBFrame();
	GContext& Context = *GContext::GetInstance();
	Context.UnbindRenderTargets();
	Context.SetRenderTargets(FrameBufferRTV.Get(), DepthStencilView.Get());

	SetViewportAndScissor(ViewportInfo);
	Editor->DrawWindows(DeltaTime);

	auto EndDraw = Clock::now();
	float CurDrawMs = std::chrono::duration<float, std::milli>(EndDraw - StartDraw).count();
	DrawTimeMs = (DrawTimeMs * 0.9f) + (CurDrawMs * 0.1f);

	EndFrame();

}

// 다중 View
void FRenderer::Render(float DeltaTime, FEditor* Editor, UScene* Scene, const TArray<FRenderView>& Views)
{
	using Clock = std::chrono::high_resolution_clock;
	if (!IsRenderReady() || !Editor || !Scene)
	{
		return;
	}
	GPUTimer.BeginFrame(DeviceContext);

	auto StartDraw = Clock::now();
	BeginFrame();
	Editor->DrawMenu();


	for (const FRenderView& View : Views)
	{
		//TODO:ViewRenderer구현 후 RenderOneView(Editor, Scene, View); 로 교체해주면 됨.
		RenderOneView(Editor, Scene, View);
	}

	FinishHZBFrame();
	// Editor->DrawLayout() 이후에 GetRenderView()를 해야 현재 프레임 기준으로 계산이 됩니다. 지금은 한 프레임 밀리는 상태

	SetViewportAndScissor(ViewportInfo);

	if (Editor->GetSplitter())
	{
		Editor->GetSplitter()->Render();
	}

	// UI 렌더링
	for (auto Item : Editor->GetWindows())
	{
		Item->Render(DeltaTime);
	}
	Editor->DrawWindows(DeltaTime);
	auto EndDraw = Clock::now();
	float CurDrawMs = std::chrono::duration<float, std::milli>(EndDraw - StartDraw).count();
	DrawTimeMs = (DrawTimeMs * 0.9f) + (CurDrawMs * 0.1f);


	EndFrame();
}

// Viewport,Scissor 설정
void FRenderer::SetViewportAndScissor(const D3D11_VIEWPORT& Viewport)
{
	GContext::GetInstance()->SetViewportAndScissor(Viewport);
}

bool FRenderer::CreateHZBConstantBuffer()
{
	D3D11_BUFFER_DESC Desc{};
	Desc.ByteWidth = sizeof(FHZBConstants);
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	return SUCCEEDED(D3DDevice->CreateBuffer(&Desc, nullptr, HZBConstantBuffer.GetAddressOf()));
}

bool FRenderer::BuildHZBMip0()
{
	if (!DepthStencilSRV || !HZBConstantBuffer || HierarchicalZBuffer.GetMipCount() == 0)
	{
		return false;
	}

	GResourceManager& Resources = *GResourceManager::GetInstance();
	ID3D11ComputeShader* CopyDepthShader = Resources.GetComputeShader(FName("HZB.CopyDepth"));
	ID3D11UnorderedAccessView* OutputMip = HierarchicalZBuffer.GetMipUAV(0);
	if (!CopyDepthShader || !OutputMip) { return false; }
	
	const FHZBConstants Constants{HierarchicalZBuffer.GetWidth(), HierarchicalZBuffer.GetHeight()};

	DeviceContext->UpdateSubresource(HZBConstantBuffer.Get(), 0, nullptr, &Constants, 0, 0);
	DeviceContext->CSSetShader(CopyDepthShader, nullptr, 0);
	DeviceContext->CSSetConstantBuffers(0, 1, HZBConstantBuffer.GetAddressOf());

	ID3D11ShaderResourceView* SourceDepth = DepthStencilSRV.Get();
	DeviceContext->CSSetShaderResources(0, 1, &SourceDepth);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &OutputMip, nullptr);

	const uint32 GroupCountX = (HierarchicalZBuffer.GetWidth() + 7) / 8;
	const uint32 GroupCountY = (HierarchicalZBuffer.GetHeight() + 7) / 8;

	DeviceContext->Dispatch(GroupCountX, GroupCountY, 1);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11UnorderedAccessView* NullUAV = nullptr;
	ID3D11Buffer* NullBuffer = nullptr;

	DeviceContext->CSSetShaderResources(0, 1, &NullSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
	DeviceContext->CSSetConstantBuffers(0, 1, &NullBuffer);
	DeviceContext->CSSetShader(nullptr, nullptr, 0);

	return BuildHZBMips();
}

bool FRenderer::BuildHZBMips()
{
	if (!HZBConstantBuffer || HierarchicalZBuffer.GetMipCount() == 0) return false;
	if (HierarchicalZBuffer.GetMipCount() == 1) return true;

	GResourceManager& Resources = *GResourceManager::GetInstance();
	ID3D11ComputeShader* DownsampleShader = Resources.GetComputeShader(FName("HZB.DownsampleMax"));
	if (!DownsampleShader) return false;

	DeviceContext->CSSetShader(DownsampleShader, nullptr, 0);
	DeviceContext->CSSetConstantBuffers(0, 1, HZBConstantBuffer.GetAddressOf());

	for (uint32 DestinationMip = 1; DestinationMip < HierarchicalZBuffer.GetMipCount(); ++DestinationMip)
	{
		const uint32 SourceMip = DestinationMip - 1;
		ID3D11ShaderResourceView* SourceHiZ = HierarchicalZBuffer.GetMipSRV(SourceMip);
		ID3D11UnorderedAccessView* DestinationUAV = HierarchicalZBuffer.GetMipUAV(DestinationMip);

		const uint32 SourceWidth = (HierarchicalZBuffer.GetWidth() >> SourceMip) > 0 ? (HierarchicalZBuffer.GetWidth() >> SourceMip) : 1;
		const uint32 SourceHeight = (HierarchicalZBuffer.GetHeight() >> SourceMip) > 0 ? (HierarchicalZBuffer.GetHeight() >> SourceMip) : 1;

		const uint32 DestinationWidth = SourceWidth > 1 ? SourceWidth / 2 : 1;
		const uint32 DestinationHeight = SourceHeight > 1 ? SourceHeight / 2 : 1;
		const FHZBConstants Constants{ SourceWidth, SourceHeight, SourceMip, 0 };

		DeviceContext->UpdateSubresource(HZBConstantBuffer.Get(), 0, nullptr, &Constants, 0, 0);
		DeviceContext->CSSetShaderResources(0, 1, &SourceHiZ);
		DeviceContext->CSSetUnorderedAccessViews(0, 1, &DestinationUAV, nullptr);
		DeviceContext->Dispatch((DestinationWidth + 7) / 8, (DestinationHeight + 7) / 8, 1);

		ID3D11ShaderResourceView* NullSRV = nullptr;
		ID3D11UnorderedAccessView* NullUAV = nullptr;

		DeviceContext->CSSetShaderResources(0, 1, &NullSRV);
		DeviceContext->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
	}
	ID3D11Buffer* NullBuffer = nullptr;
	DeviceContext->CSSetConstantBuffers(0, 1, &NullBuffer);
	DeviceContext->CSSetShader(nullptr, nullptr, 0);

	return true;
}

// DSV 바인딩을 해제하고 다음 프레임에서 읽을 Hi-Z를 생성한다.
void FRenderer::FinishHZBFrame()
{
	GContext& Context = *GContext::GetInstance();
	Context.UnbindRenderTargets();

	// 생성 경로가 실패하면 다음 프레임은 Hi-Z로 객체를 숨기지 않는다.
	//bHZBValid = BuildHZBMip0();
	const bool bMip0Built = BuildHZBMip0();
	bHZBValid = bMip0Built && BuildHZBMips();

	Context.SetRenderTargets(FrameBufferRTV.Get(), DepthStencilView.Get());
}
