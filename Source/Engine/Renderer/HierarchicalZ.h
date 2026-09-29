#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Container/Array.h"

class FHierarchicalZBuffer
{
public:
	bool Create(ID3D11Device* Device, uint32 Width, uint32 Height);
	bool Resize(ID3D11Device* Device, uint32 Width, uint32 Height);
	void Release();

	uint32 GetWidth() const { return Width; }
	uint32 GetHeight() const { return Height; }
	uint32 GetMipCount() const { return MipCount; }

	ID3D11ShaderResourceView* GetSRV() const { return ShaderResourceView.Get(); }
	ID3D11UnorderedAccessView* GetMipUAV(uint32 MipIndex) const; // mip에 쓰기 위한 UAV 반환
	ID3D11ShaderResourceView* GetMipSRV(uint32 MipIndex) const;
private:
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 MipCount = 0;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
	TArray<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> MipUAVs;
	TArray<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> MipSRVs;
};