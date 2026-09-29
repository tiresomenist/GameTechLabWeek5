#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Math/Matrix.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Renderer/Line/LineBatcher.h"
#include "Engine/Renderer/RenderView.h"
#include "Engine/Renderer/ViewRenderData.h"
#include "Engine/Renderer/Occlusion.h"
#include "Core/Container/Map.h"
#include "PipelineStateCache.h"
#include "PassDrawBuilder.h"
#include "OpaqueDrawSorter.h"
#include "ConstantBufferRing.h"
#include "ConstantBufferManager.h"
#include "PassExecutor.h"
#include "PreparedDraw.h"
#include "Core/Container/Array.h"
#include "Engine/Util/RenderSubmissionStats.h"

class UScene;
class FEditor;
class UCameraComponent;
struct FShaderResource;
struct FVertexTexture;
enum class EViewportType;
struct FViewRenderData;

struct FHZBCullConstants
{
	FMatrix ViewProjection;
	uint32 CellCount = 0;
	float ViewportWidth = 0.0f;
	float ViewportHeight = 0.0f;
	uint32 HZBMipCount = 0;
	float ViewportTopLeftX = 0.0f;
	float ViewportTopLeftY = 0.0f;
	uint32 Padding0 = 0;
	uint32 Padding1 = 0;
};
struct FHZBViewInput
{
	ID3D11ShaderResourceView* Texture = nullptr;
	const UScene* Scene = nullptr;
	uint32 MipCount = 0;
	uint32 ViewId = 0;
	uint64 FrameIndex = 0;
	uint64 Generation = 0;
	bool bValid = false;
};

struct FHZBViewState
{
	FHZBOcclusionCuller Culler;
	FRenderViewSnapshot PreviousView{};
	const UScene* PreviousScene = nullptr;
	uint64 PreviousFrameIndex = 0;
	uint64 PreviousGeneration = 0;
	bool bHasPreviousView = false;
};
// 출력 타깃은 호출자가 준비한다. 각 View는 데이터를 수집한 직후 그린다.
class FViewRenderer
{
public:
	void Create(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	void Shutdown();
	void RenderView(const FViewRenderData& Data, const FHZBViewInput& HZB);
	void FilterGridCellCandidates(const FRenderViewSnapshot& View,
		const FHZBViewInput& HZB, const TArray<FGridCellCandidate>& Candidates,
		TArray<FGridCellCandidate>& OutRenderGridCells);

	// 프레임 전체의 제출 통계를 초기화한다.
	void BeginSubmissionFrame()
	{
		SubmissionStats.BeginFrame();
	}

	// 모든 View에서 누적한 제출 통계를 반환한다.
	const FRenderSubmissionCounts& GetSubmissionCounts() const
	{
		return SubmissionStats.GetCounts();
	}
	void ReleaseViewReferences()
	{
		// 배열 용량은 유지하면서 Source와 Material 참조를 제거합니다.
		PassDraws.Clear();
		OpaqueSortScratch.Empty();
		PassDrawBuilder.ReleaseViewReferences();
	}
	void SetHZBOcclusionEnabled(bool bEnabled) { bEnableHZBOcclusion = bEnabled; }
	bool IsHZBOcclusionEnabled() const { return bEnableHZBOcclusion; }

private:
	bool CreateShaders();
	void ReleaseShaders();
	void CreateConstantBuffer();
	void ReleaseConstantBuffer();
	void CreateRasterizerState();
	void ReleaseRasterizerState();
	void CreateAlphaBlendState();
	void ReleaseAlphaBlendState();
	void CreateDepthStencilStates();
	void ReleaseDepthStencilStates();
	void CreateTextResources();
	void ReleaseTextResources();

	void SetViewportAndScissor(const D3D11_VIEWPORT& Viewport);
	void UpdateTransformConstantBuffer(const FMatrix& World, const FMatrix& VP);
	void UpdateMaterialConstants(const FPrimitiveRenderData& Data);
	void BindShader(const FShaderResource& Shader);
	void BindPrimitiveBuffers(const FPrimitiveRenderData& Data);
	bool BindMaterial(const FMaterial& Material);
	void RenderPrimitive(const FPrimitiveRenderData& Data, EViewModeIndex InViewMode, bool bWriteStencil = false);
	void RenderHighlight(const FPrimitiveRenderData& Data);
	void RenderOutline(const FPrimitiveRenderData& Data);
	void RenderGizmo(const FPrimitiveRenderData& Data);
	void RenderBatchLine(const FMatrix& ViewProj);
	bool UpdateTextVertexBuffer(const TArray<FVertexTexture>& Vertices);
	void RenderText(UINT IndexCount);

	// Device와 Context는 비소유 참조.
	ID3D11Device* D3DDevice = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;

	// ResourceManager 소유 자원의 비소유 참조.
	ID3D11RasterizerState* DefaultRasterizerState = nullptr;
	ID3D11RasterizerState* CullFrontRasterizerState = nullptr;
	ID3D11RasterizerState* CullNoneRasterizerState = nullptr;
	ID3D11RasterizerState* WireframeRasterizerState = nullptr;
	ID3D11DepthStencilState* DefaultDepthStencilState = nullptr;
	ID3D11DepthStencilState* GizmoDepthStencilState = nullptr;
	ID3D11DepthStencilState* HighlightDepthStencilState = nullptr;
	ID3D11DepthStencilState* StencilWriteDepthStencilState = nullptr;
	ID3D11DepthStencilState* OutlineDepthStencilState = nullptr;
	ID3D11DepthStencilState* TextDepthStencilState = nullptr;
	ID3D11DepthStencilState* TranslucentDepthStencilState = nullptr;
	ID3D11BlendState* AlphaBlendState = nullptr;
	ID3D11BlendState* AdditiveBlendState = nullptr;
	const FShaderResource* SimpleShader = nullptr;
	ID3D11PixelShader* WireframePixelShader = nullptr;
	const FShaderResource* HighlightShader = nullptr;
	const FShaderResource* GridShader = nullptr;
	const FShaderResource* BatchLineShader = nullptr;
	const FShaderResource* TextShader = nullptr;
	ID3D11SamplerState* FontSamplerState = nullptr;

	// View 사이에서 순서대로 재사용하는 작업 버퍼를 소유한다.
	Microsoft::WRL::ComPtr<ID3D11Buffer> TransformConstantBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> GridConstantBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> TextVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> TextIndexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> MaterialConstantBuffer;

	UINT TextVertexCapacity = 0;
	TArray<FVertexTexture> TextVertexScratch;
	TArray<uint32> TextObjectVisibility;

	// 부족할 때만 텍스트 VB와 고정 패턴 IB를 함께 확장한다.
	bool EnsureTextCapacity(UINT RequiredVertices);

	// 전체 정점을 업로드하며 실패하면 이번 텍스트 Draw를 생략한다.

	// 메시 가시성과 텍스트 Bounds를 반영하여 해당 View의 텍스트를 출력한다.
	void RenderVisibleText(const FViewRenderData& Data);

	FLineBatcher LineBatcher;
	void PreparePrimitiveVisibility(const FViewRenderData& Data, const FHZBViewInput& HZB);
	FPipelineStateCache PipelineStateCache;
	FPassDrawBuilder PassDrawBuilder;
	FConstantBufferRing CBRingBuffer;
	FConstantBufferManager CBManager;
	FPassExecutor PassExecutor;
	FPassDrawList PassDraws;
	TArray<FPreparedDraw> OpaqueSortScratch;
	FRenderSubmissionStats SubmissionStats;

	TArray<uint32> PrimitiveVisibility;
	TMap<uint32, FHZBViewState> HZBViewStates;
	Microsoft::WRL::ComPtr<ID3D11Buffer> HZBCullConstantBuffer;
	bool bEnableHZBOcclusion = true;
};
