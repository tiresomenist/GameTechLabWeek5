#pragma once

#include <d3d11.h>
#include "Core/Core.h"

inline constexpr uint32 InvalidRenderId = static_cast<uint32>(-1);

enum class EVertexFormat : uint32
{
    Simple,     // FVertexSimple
    Texture,    // FVertexTexture
    PNCT,       // FVertexPNCT
};

// 메시 전체가 페이지 안에서 차지하는 영역.
// FMeshResource가 보관한다.
struct FMeshAllocation
{
    uint32 MeshPageId = InvalidRenderId;

    // 페이지 IB에서 이 메시가 시작하는 위치.
    // 바이트가 아니라 uint32 인덱스 원소 단위.
    uint32 FirstIndex = 0;

    // 페이지 VB에서 이 메시가 시작하는 위치.
    // 바이트가 아니라 정점 원소 단위.
    int32 BaseVertex = 0;

    uint32 VertexCount = 0;
    uint32 IndexCount = 0;
};

// 실제 DrawIndexed 한 번에 필요한 범위.
// 작업자 2가 섹션 정보를 반영해서 완성한다.
struct FMeshDrawRange
{
    uint32 MeshPageId = InvalidRenderId;

    uint32 FirstIndex = 0;
    uint32 IndexCount = 0;
    int32 BaseVertex = 0;
};

// 작업자 1이 페이지를 바인딩할 때 조회하는 정보.
// GPU 리소스 소유권은 넘기지 않는다.
struct FMeshPageBinding
{
    ID3D11Buffer* VertexBuffer = nullptr;
    ID3D11Buffer* IndexBuffer = nullptr;

    uint32 Stride = 0;

    DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;
    EVertexFormat VertexFormat = EVertexFormat::Simple;
};

enum EPrimitiveRenderFlags : uint32
{
    Primitive_None = 0,
    Primitive_Selected = 1u << 0,
    Primitive_AllowOutline = 1u << 1,
};