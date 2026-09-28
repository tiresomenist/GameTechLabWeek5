#pragma once

#include <d3d11.h>
#include "Core/Math/Vector.h"
#include "Engine/Renderer/Material.h"
#include "Engine/Renderer/RenderConstants.h"

struct FMatrix; 

//struct FPrimitiveRenderData
//{
//	//장기적으로 렌더패스,인풋레이아웃,셰이더,블렌드,뎁스스텐실,라스터라이저
//	//텍스처,샘플러,머티리얼 데이터(PBR?), 첫 인덱스, 인덱스 수, 기반 버텍스
//	//uv 등등을 같이 묶어서 보내면 일종의 패킷으로 작용할수 있지않을까
//	//그리고 설정 변경되는 순서를 compare같은 함수를 따로 달아주면 정렬이나 비교에 사용할수있어서 sort할수있지않을까
//
//	ID3D11Buffer*				VertexBuffer = nullptr;
//	ID3D11Buffer*				IndexBuffer = nullptr;
//	UINT						Stride = 0;
//	UINT						IndexStart = 0;
//	UINT						IndexCount = 0;
//	D3D11_PRIMITIVE_TOPOLOGY	Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
//
//	FMaterial* Material;
//
//	const FMatrix*				WorldMatrix = nullptr;		// 컴포넌트가 소유한 월드행렬 가리키기
//};

struct FPrimitiveRenderData
{
	FMeshDrawRange Geometry{};
	const FMaterial* Material = nullptr;
	uint32 ObjectIndex = InvalidRenderId;
	uint32 Flags = Primitive_AllowOutline;
	D3D11_PRIMITIVE_TOPOLOGY Topology =	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//Topology에 대해, 회전 기즈모는 LINELIST로 바꿔주면 됩니다.
};