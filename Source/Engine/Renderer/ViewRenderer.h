#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Math/Matrix.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Renderer/ViewSettings.h"
#include "Engine/Renderer/Line/LineBatcher.h"

class UScene;
class FEditor;
class UCameraComponent;
struct FShaderResource;
struct FVertexTexture;
enum class EViewportType;

struct FRenderView
{
	UCameraComponent* Camera = nullptr;
	D3D11_VIEWPORT Viewport{};
	FViewSettings ViewSettings{};
	bool bDrawEditorGizmos = false;
	EViewportType ViewType;
};

// 출력 타깃은 호출자가 준비한다. 각 View는 데이터를 수집한 직후 그린다.
class FViewRenderer
{
public:
	void Create(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	void Shutdown();
	void RenderView(FEditor* Editor, UScene* Scene, const FRenderView& View);

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
	void UpdateTextVertexBuffer(TArray<FVertexTexture>& Vertices);
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
	static const UINT MaxTextVertices = 8192;
	FLineBatcher LineBatcher;
};
