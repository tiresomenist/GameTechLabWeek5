#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Container/Array.h"
#include "Engine/Renderer/ViewRenderer.h"
#include "Engine/Renderer/GPUTimer.h"
#include "Engine/Renderer/RenderView.h"
#include "Engine/Renderer/ViewRenderData.h"
#include "Engine/Renderer/HierarchicalZ.h"

class GDevice;
class FEditor;
class UScene;

class FRenderer
{
public:
	GDevice* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
	ID3D11Device* D3DDevice = nullptr;
	FLOAT ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

	bool bImGuiContextCreated = false;
	bool bImGuiWin32Initialized = false;
	bool bImGuiDX11Initialized = false;

	void Create(HWND HWnd, GDevice* InDevice, uint32 Width, uint32 Height);
	void Shutdown();
	void OnResize(uint32 Width, uint32 Height);
	bool IsRenderReady() const;
	const D3D11_VIEWPORT& GetViewport() const;
	void PrepareRTVDSV();
	void BeginFrame();
	void EndFrame();
	void Render(float DeltaTime, FEditor* Editor, UScene* Scene);
	void Render(float DeltaTime, FEditor* Editor, UScene* Scene, const TArray<FRenderView>& Views);

	float GetDrawTimeMs() const { return DrawTimeMs; }
	float GetGPUTimeMs() const { return GPUTimer.GetGPUTimeMs(); }
	float GetGPUWaitMs() const { return GPUWaitMs; }

	const FRenderSubmissionCounts& GetSubmissionCounts() const
	{
		return ViewRenderer.GetSubmissionCounts();
	}

	void InvalidateOcclusionHistory()
	{
		bHZBValid = false;
		++HZBGeneration;
	}

	void SetHZBOcclusionEnabled(bool bEnabled) { ViewRenderer.SetHZBOcclusionEnabled(bEnabled); }
	bool IsHZBOcclusionEnabled() const { return ViewRenderer.IsHZBOcclusionEnabled(); }
	void InvalidateOcclusionCells(const TArray<uint64>& CellKeys)
	{
		ViewRenderer.InvalidateOcclusionCells(CellKeys);
	}

private:
	bool CreateSwapChain(HWND HWnd, uint32 Width, uint32 Height);
	bool CreateFrameBuffer();
	void ReleaseFrameBuffer();
	bool CreateDepthStencilBuffer(uint32 Width, uint32 Height);
	void ReleaseDepthStencilBuffer();
	void SetViewportAndScissor(const D3D11_VIEWPORT& Viewport);
	void SwapBuffer();

	bool CreateHZBConstantBuffer();
	bool BuildHZBMip0();
	bool BuildHZBMips();
	void FinishHZBFrame();

	FViewRenderer ViewRenderer;
	FViewRenderData ViewData;

	void RenderOneView(FEditor* Editor, UScene* Scene, const FRenderView& View);
	Microsoft::WRL::ComPtr<IDXGISwapChain> SwapChain;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> FrameBuffer;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> FrameBufferRTV;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> DepthStencilBuffer;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthStencilView;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> DepthStencilSRV;
	Microsoft::WRL::ComPtr<ID3D11Buffer> HZBConstantBuffer;

	D3D11_VIEWPORT ViewportInfo{};
	bool bRenderReady = false;
	bool bGraphicsFailed = false;
	bool bTearingSupported = false;

	FGPUTimer GPUTimer;
	float DrawTimeMs = 0.0f;
	float GPUWaitMs = 0.0f;

	FHierarchicalZBuffer HierarchicalZBuffer;

	bool bHZBValid = false;
	uint64 HZBFrameIndex = 0;
	uint64 HZBGeneration = 1;
};
