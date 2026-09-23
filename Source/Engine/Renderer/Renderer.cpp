#include "pch.h"
#include "Renderer.h"
#include "Engine/Renderer/Device.h"
#include "Engine/Renderer/Context.h"
#include "Engine/Log.h"
#include "Editor/Editor.h"
#include "Editor/Window/EditorWindow.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <stdexcept>
#include <utility>
#include <cstdlib>
#include <chrono>

using Microsoft::WRL::ComPtr;

void FRenderer::Create(HWND HWnd, GDevice* InDevice, uint32 Width, uint32 Height)
{
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
		!CreateDepthStencilBuffer(static_cast<uint32>(ViewportInfo.Width),static_cast<uint32>(ViewportInfo.Height)))
	{
		throw std::runtime_error("Failed to create renderer output");
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

	ReleaseDepthStencilBuffer();
	ReleaseFrameBuffer();
	SwapChain.Reset();
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

	using Clock = std::chrono::high_resolution_clock;
	auto StartWait = Clock::now();

	const HRESULT Result = SwapChain->Present(0, 0);

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

	D3D11_TEXTURE2D_DESC DepthStencilDesc{};
	DepthStencilDesc.Width = Width;
	DepthStencilDesc.Height = Height;
	DepthStencilDesc.MipLevels = 1;
	DepthStencilDesc.ArraySize = 1;
	DepthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DepthStencilDesc.SampleDesc.Count = 1;
	DepthStencilDesc.SampleDesc.Quality = 0;
	DepthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	DepthStencilDesc.CPUAccessFlags = 0;
	DepthStencilDesc.MiscFlags = 0;

	HRESULT Result = D3DDevice->CreateTexture2D(&DepthStencilDesc, nullptr, NewDepthBuffer.GetAddressOf());

	if (FAILED(Result)){ return false; }

	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc{};
	DSVDesc.Format = DepthStencilDesc.Format;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;

	Result = D3DDevice->CreateDepthStencilView( NewDepthBuffer.Get(),&DSVDesc, NewDSV.GetAddressOf());

	if (FAILED(Result))
	{
		UE_LOG("[FRenderer] Failed to create depth stencil view. HRESULT: {}\n",Result);
		return false;
	}

	DepthStencilBuffer = std::move(NewDepthBuffer);
	DepthStencilView = std::move(NewDSV);

	return true;
}

void FRenderer::ReleaseDepthStencilBuffer()
{
	DepthStencilView.Reset();
	DepthStencilBuffer.Reset();
}

void FRenderer::OnResize(uint32 Width, uint32 Height)
{
	bRenderReady = false;

	GContext& Context = *GContext::GetInstance();

	if (Width == 0 || Height == 0 || !D3DDevice || !Context.IsInitialized() || !SwapChain.Get() || bGraphicsFailed)
	{ return; }

	Context.UnbindRenderTargets();

	ReleaseDepthStencilBuffer();
	ReleaseFrameBuffer();

	Context.Flush();

	const HRESULT Result = SwapChain->ResizeBuffers(0,Width,Height,	DXGI_FORMAT_UNKNOWN, 0);

	if (FAILED(Result))
	{
		bGraphicsFailed = true;
		PostQuitMessage(EXIT_FAILURE);
		return;
	}

	ViewportInfo = {0.0f,0.0f,static_cast<float>(Width),static_cast<float>(Height),0.0f,1.0f};

	if (!CreateFrameBuffer() || !CreateDepthStencilBuffer(Width, Height))
	{
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
		ViewRenderer.RenderView(Editor, Scene, View);
	}

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
		ViewRenderer.RenderView(Editor, Scene, View);
	}

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
	GContext::GetInstance()->SetViewport(Viewport);

	// Viewport는 정수 픽셀 경계로 구성한다.
	const D3D11_RECT ScissorRect
	{
		static_cast<LONG>(Viewport.TopLeftX),
		static_cast<LONG>(Viewport.TopLeftY),
		static_cast<LONG>(Viewport.TopLeftX + Viewport.Width),
		static_cast<LONG>(Viewport.TopLeftY + Viewport.Height)
	};

	DeviceContext->RSSetScissorRects(1, &ScissorRect);
}
