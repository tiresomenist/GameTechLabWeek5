#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma once
#include <unordered_map>
#include <map>
#include <string>
#include <span>
#include "Engine/Renderer/Device.h"
#include "Core/Core.h"
#include "Core/Container/String.h"
#include "Core/Container/Array.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Engine/Resource/MeshResource.h"
#include "Engine/Renderer/Text/FontAtlas.h"
#include "Core/Container/Map.h"
#include "Core/Name/Name.h"
#include "Engine/Resource/ShaderResource.h"
#include "Engine/Renderer/Material.h"
#include "Engine/Resource/StaticMesh.h"
#include "Engine/Resource/StaticMeshData.h"
#include "Core/Util/Objimporter.h"
#include "Engine/Object/ObjectFactory.h"
#include <wrl/client.h>
#include <filesystem>
#include <cstddef>
#include <functional>

class FTextureResource;

// 프리로드 진행 상황과 최종 결과를 전달하며 엔진 리소스 포인터는 보관하지 않는다.
struct FStaticMeshPreloadResult
{
	std::size_t TotalCount = 0;
	std::size_t LoadedCount = 0;
	std::size_t SkippedCount = 0;
	std::size_t FailedCount = 0;
	std::size_t SearchErrorCount = 0;

	// 검색이 끝나야 TotalCount를 기준으로 진행률을 계산할 수 있다.
	bool bSearchComplete = false;
	bool bCancelled = false;
	std::filesystem::path CurrentFile;
};

class GResourceManager
{
public:
	static GResourceManager* GetInstance();

	void Initialize(GDevice* InDevice);
	void Shutdown();
	FMeshResource* CreateMesh(const FName& MeshName, std::span<const FVertexSimple> Vertices, std::span<const uint32> Indices);
	// 위치와 UV 정점으로 삼각형 메시를 생성하며 실패 시 nullptr을 반환함
	FMeshResource* CreateTexturedMesh(const FName& MeshName, std::span<const FVertexTexture> Vertices, std::span<const uint32> Indices);
	FMeshResource* CreateStaticMeshResource(const FName& MeshName, std::span<const FVertexPNCT> Vertices, std::span<const uint32> Indices);
	
	FMeshResource* GetPrimitive(const FName& MeshName);
	FFontAtlas* GetDefaultFont() { return DefaultFont.GetSRV() ? &DefaultFont : nullptr; }
	FTextureResource* GetOrLoadTexture(const FString& FilePath);
	UStaticMesh* GetOrLoadStaticMesh(const FName& MeshKey);
	
	// 일반 조회도 같은 키 해석과 로딩 경로를 사용한다.
	UStaticMesh* GetStaticMesh(const FName& Key)
	{
		return GetOrLoadStaticMesh(Key);
	}

	// 문자열 요청을 기존 FName 조회 경로로 전달한다.
	UStaticMesh* GetStaticMesh(const FString& FilePath)
	{
		return GetStaticMesh(FName(FilePath));
	}
	void RegisterShader(const FName& Name,const WCHAR* FilePath,const char* VSEntry,
		const char* PSEntry,const TArray<D3D11_INPUT_ELEMENT_DESC>& Layout);
	void RegisterBlendState(const FName& Name,const D3D11_BLEND_DESC& Desc);
	ID3D11BlendState* GetBlendState(const FName& Name) const;

	const FShaderResource* GetShader(const FName& Name) const;
	ID3D11PixelShader* GetWireframePixelShader() const;

	void RegisterSampler(const FName& Name, const D3D11_SAMPLER_DESC& Desc);

	ID3D11SamplerState* GetSampler(const FName& Name) const;
	FMaterial CreateColorMaterial() const;
	FMaterial CreateTextureMaterial(ID3D11ShaderResourceView* SRV) const;
	FMaterial CreateStaticMeshMaterial(ID3D11ShaderResourceView* SRV, const FString& InTexturePath = "", bool bClamp = false) const;	void RegisterDefaultPrimitives(GDevice* InDevice);
	void RegisterTexturePrimitives(GDevice* InDevice);
	void RegisterRasterizerState(const FName& Name, const D3D11_RASTERIZER_DESC& Desc);
	ID3D11RasterizerState* GetRasterizerState(const FName& Name) const;
	void RegisterDepthStencilState(const FName& Name, const D3D11_DEPTH_STENCIL_DESC& Desc);
	ID3D11DepthStencilState* GetDepthStencilState(const FName& Name) const;

	// 메인 스레드에서 바이너리 캐시만 미리 로딩하고 진행 상황과 취소 요청을 처리한다.
	FStaticMeshPreloadResult PreloadCachedStaticMeshes(
		const std::filesystem::path& Root,
		const std::function<void(const FStaticMeshPreloadResult&)>& OnProgress = {},
		const std::function<bool()>& ShouldCancel = {});
private:
	GResourceManager() = default;
	~GResourceManager() = default;
	GResourceManager(const GResourceManager&) = delete;
	GResourceManager& operator=(const GResourceManager&) = delete;
	void RegisterDefaultRenderResources();
	void RegisterDefaultRasterizerStates();
	void RegisterDefaultBlendStates();
	void RegisterDefaultDepthStencilStates();
	// 정규화한 절대 OBJ 경로를 기준으로 CPU 데이터를 GPU 메시로 만들고 등록한다.
	UStaticMesh* BuildAndCacheStaticMesh(const std::filesystem::path& ObjPath, FStaticMeshData& MeshData);
	Microsoft::WRL::ComPtr<ID3D11Buffer> TextureMaterialConstantBuffer;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> WireframePixelShader;
	TMap<FName, Microsoft::WRL::ComPtr<ID3D11RasterizerState>> RasterizerStateCache;
	TMap<FName, FShaderResource> ShaderCache;
	TMap<FName, Microsoft::WRL::ComPtr<ID3D11SamplerState>> SamplerCache;
	TMap<FName, Microsoft::WRL::ComPtr<ID3D11BlendState>> BlendStateCache;
	TMap<FName, Microsoft::WRL::ComPtr<ID3D11DepthStencilState>> DepthStencilStateCache;
	GDevice* Device = nullptr;

	TMap<FName, FMeshResource*> PrimitiveCache;
	TMap<FString, FTextureResource*> TextureCache;
	// 정규화한 절대 OBJ 경로로 완성된 메시를 조회한다.
	TMap<FName, UStaticMesh*> StaticMeshCache;
	// 성공한 요청의 원래 키를 절대 경로 키에 연결하여 반복적인 파일 검사를 피한다.
	TMap<FName, FName> StaticMeshAliases;
	FFontAtlas DefaultFont;
};

