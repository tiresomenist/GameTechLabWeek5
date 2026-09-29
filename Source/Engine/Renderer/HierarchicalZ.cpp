#include "pch.h"
#include "Engine/Renderer/HierarchicalZ.h"

bool FHierarchicalZBuffer::Create(ID3D11Device* Device, uint32 InWidth, uint32 InHeight)
{
	Release();
	return Resize(Device, InWidth, InHeight);
}

bool FHierarchicalZBuffer::Resize(ID3D11Device* Device, uint32 InWidth, uint32 InHeight)
{
	if (!Device || InWidth == 0 || InHeight == 0)
	{
		return false;
	}
	if (Texture && Width == InWidth && Height == InHeight)
	{
		return true;
	}
	Release();

	uint32 LargestDimension = InWidth > InHeight ? InWidth : InHeight;
	MipCount = 1;
	while (LargestDimension > 1)
	{
		LargestDimension >>= 1;
		++MipCount;
	}

	// 모든 Mip을 담을 HZB texture
	D3D11_TEXTURE2D_DESC TextureDesc{};
	TextureDesc.Width = InWidth;
	TextureDesc.Height = InHeight;
	TextureDesc.MipLevels = MipCount;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R32_FLOAT;  // Depth 보관
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, Texture.GetAddressOf())))
	{
		Release();
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = MipCount;
	if (FAILED(Device->CreateShaderResourceView(Texture.Get(), &SRVDesc, ShaderResourceView.GetAddressOf())))
	{
		Release();
		return false;
	}

	// Mip 생성
	MipSRVs.Reserve(MipCount);
	MipUAVs.Reserve(MipCount);
	for (uint32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};  // 특정 Mip 하나에 쓰는 창
		UAVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice = MipIndex;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> MipUAV;
		if (FAILED(Device->CreateUnorderedAccessView(Texture.Get(), &UAVDesc, MipUAV.GetAddressOf())))
		{
			Release();
			return false;
		}
		MipUAVs.Add(std::move(MipUAV));

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MostDetailedMip = MipIndex;
		SRVDesc.Texture2D.MipLevels = 1;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> MipSRV;
		if (FAILED(Device->CreateShaderResourceView(Texture.Get(), &SRVDesc, MipSRV.GetAddressOf())))
		{
			Release();
			return false;
		}
		MipSRVs.Add(std::move(MipSRV));
	}

	Width = InWidth;
	Height = InHeight;
	return true;
}

void FHierarchicalZBuffer::Release()
{
	MipUAVs.Empty();
	MipSRVs.Empty();
	ShaderResourceView.Reset();
	Texture.Reset();
	Width = 0;
	Height = 0;
	MipCount = 0;
}

ID3D11UnorderedAccessView* FHierarchicalZBuffer::GetMipUAV(uint32 MipIndex) const
{
	if (MipIndex >= static_cast<uint32>(MipUAVs.Num()))
	{
		return nullptr;
	}
	return MipUAVs[MipIndex].Get();
}

ID3D11ShaderResourceView* FHierarchicalZBuffer::GetMipSRV(uint32 MipIndex) const
{
	if (MipIndex >= static_cast<uint32>(MipSRVs.Num()))
	{
		return nullptr;
	}
	return MipSRVs[MipIndex].Get();
}