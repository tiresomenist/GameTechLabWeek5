#pragma once

#include <d3d11.h>
#include "Core/Core.h"
#include "Core/Container/String.h"
#include "Core/Math/Vector.h"
#include "Core/Name/Name.h"

struct FShaderResource;
class FArchive;

enum class EPrimitiveBlendMode : uint32
{
	Opaque,
	Additive,
};

struct FMaterial
{
	// 모든 포인터는 비소유 참조.
	// 참조 대상은 이 Material을 사용하는 Draw가 끝날 때까지 유효해야 한다.

	ID3D11ShaderResourceView* SRV = nullptr; //SRV
	FString TexturePath;

	const FShaderResource* Shader = nullptr;	//VS,PS,InputLayout을 묶은 구조체
	ID3D11SamplerState* Sampler = nullptr;	//Sampler
	FName SamplerName = FName("LinearWrap");

	EPrimitiveBlendMode BlendMode = EPrimitiveBlendMode::Opaque;	//Blend mode
	
	FVector4 DiffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
	float AlphaCutoff = 0.0f;

	bool bEnableUVScroll = false;
	FVector2 UVScale{ 1.0f, 1.0f };
	FVector2 UVOffset{ 0.0f, 0.0f };
	FVector2 ScrollSpeed{ 0.0f, 0.0f };
	
	ID3D11Buffer* ConstantBuffer = nullptr;	//UV·Tint·AlphaCutoff등을 담을 constantBuffer

	void SetTexture(const FString& InPath);
	void SetSampler(const FName& InName);

	void SetUVScale(FVector2 InUVScale)
	{
		UVScale = InUVScale;
	}

	void SetUVOffset(FVector2 InUVOffset)
	{
		UVOffset = InUVOffset;
	}

	void SetUVScrollSpeed(FVector2 InUVScrollSpeed)
	{
		ScrollSpeed = InUVScrollSpeed;
	}

	void SetDiffuseColor(FVector4 InDiffuseColor)
	{
		DiffuseColor = InDiffuseColor;
	}

	void SetAlphaCutoff(float InAlphaCutoff)
	{
		AlphaCutoff = InAlphaCutoff;
	}

	void Serialize(FArchive& Archive);
};