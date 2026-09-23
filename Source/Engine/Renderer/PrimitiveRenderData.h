#pragma once

#include <d3d11.h>
#include "Core/Math/Vector.h"
#include "Engine/Renderer/Material.h"

struct FMatrix; 

// HLSL의 float2 크기와 float2 오프셋에 대응하는 16바이트 상수
struct FTextureUVTransform
{
    FVector2 Scale{ 1.0f, 1.0f };
    FVector2 Offset{ 0.0f, 0.0f };

    FTextureUVTransform() = default;
    FTextureUVTransform(const FVector2& InScale, const FVector2& InOffset)
        : Scale(InScale), Offset(InOffset) {}
    FTextureUVTransform(float ScaleU, float ScaleV, float OffsetU, float OffsetV)
        : Scale(ScaleU, ScaleV), Offset(OffsetU, OffsetV) {}
};

static_assert(sizeof(FTextureUVTransform) == 16);

struct FTextureDrawConstants
{
	FTextureUVTransform UV;

	// 텍스처에 곱할 오브젝트별 색상
	FVector4 DiffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };

	// 해당 값보다 작은 알파의 픽셀을 제거함
	float AlphaCutoff = 0.0f;

	// 상수 버퍼 크기를 16바이트 배수로 맞춤
	float Padding[3]{};
};
static_assert(sizeof(FTextureDrawConstants) == 48);

struct FPrimitiveRenderData
{
	//장기적으로 렌더패스,인풋레이아웃,셰이더,블렌드,뎁스스텐실,라스터라이저
	//텍스처,샘플러,머티리얼 데이터(PBR?), 첫 인덱스, 인덱스 수, 기반 버텍스
	//uv 등등을 같이 묶어서 보내면 일종의 패킷으로 작용할수 있지않을까
	//그리고 설정 변경되는 순서를 compare같은 함수를 따로 달아주면 정렬이나 비교에 사용할수있어서 sort할수있지않을까

	ID3D11Buffer*				VertexBuffer = nullptr;
	ID3D11Buffer*				IndexBuffer = nullptr;
	UINT						Stride = 0;
	UINT						IndexStart = 0;
	UINT						IndexCount = 0;
	D3D11_PRIMITIVE_TOPOLOGY	Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	FMaterial Material{};
	const FMatrix*				WorldMatrix = nullptr;		// 컴포넌트가 소유한 월드행렬 가리키기

	bool						isSelected = false;

	FVector Min;
	FVector Max;
    // 기본값은 텍스처 전체를 사용함
    FTextureUVTransform UVTransform;

	// 텍스처에 곱할 색상
	FVector4 DiffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };

	// 0이면 알파 컷아웃을 사용하지 않음
	float AlphaCutoff = 0.0f;	//Material.AlphaCutoff와 중복코드?

	// 선택 시 일반 메시 외곽선 렌더링 허용 여부
	bool bAllowOutline = true;

	// 구형 닫힌 메시에는 cull_back, 플립북, 평면, 빌보드에는 cull_none
	bool bTwoSided = false;
};
