#pragma once 
#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Container/String.h"
#include "Core/Math/Matrix.h"
#include "Core/Container/Array.h"
class UStaticMesh;

class FImpostorBaker {
public:
	bool Bake(UStaticMesh* StaticMesh, const FString& OutputPath);

private:
	bool CreateBakeTargets();

	void ReleaseBakeTargets();

	bool RenderView(UStaticMesh* StaticMesh, const FMatrix& View, const FMatrix& ViewProjection);

	bool ReadbackTile(TArray<uint8_t>& OutPixels);

	bool SavePNG(const FString& FilePath,
		const TArray<uint8_t>& Pixels,
		uint32 Width,
		uint32 Height);

	FMatrix MakeCaptureView(const FVector& CameraLocation, const FVector& Target) const;

	FMatrix MakeCaptureProjection(float OrthoHeight, float NearZ, float FarZ) const;

private:
	ID3D11Device* D3DDevice = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> RenderTarget;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> RenderTargetView;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> DepthBuffer;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthStencilView;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> ReadbackTexture;

	Microsoft::WRL::ComPtr<ID3D11Buffer> ObjectConstantBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> MaterialConstantBuffer;

	uint32 TileWidth = 256;
	uint32 TileHeight = 256;
};