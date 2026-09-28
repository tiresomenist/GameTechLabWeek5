#pragma once

#include <span>
#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Container/Array.h"
#include "Engine/Renderer/RenderDataTypes.h"

class FMeshBufferPool
{
public:
    FMeshBufferPool() = default;
    FMeshBufferPool(const FMeshBufferPool&) = delete;
    FMeshBufferPool& operator=(const FMeshBufferPool&) = delete;

    bool Initialize(ID3D11Device* InDevice);
    void Shutdown();

    // Vertices는 VertexFormat에 해당하는 정점 배열
    // Indices의 값이 VertexCount 이상이면 에러
    // 현재 ResourceManager의 세 생성 함수에서 검증 후 호출한다.
    bool AllocateAndUpload(EVertexFormat VertexFormat, const void* Vertices, uint32 VertexCount, 
        std::span<const uint32> Indices, FMeshAllocation& OutAllocation);

    FMeshPageBinding GetPageBinding(uint32 MeshPageId) const;

private:
    // 정점 정보에 따른 VB + IB
    struct FMeshBufferPage
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer;

        // 해당하는 정점 포맷에 따라 바인딩해줘야함.
        EVertexFormat VertexFormat = EVertexFormat::Simple;
        uint32 Stride = 0;

        // 원소 개수 단위
        uint32 VertexCapacity = 0;
        uint32 IndexCapacity = 0;

        uint32 UsedVertexCount = 0;
        uint32 UsedIndexCount = 0;
    };

    static uint32 GetVertexStride(EVertexFormat VertexFormat);

    bool CreatePage(EVertexFormat VertexFormat, uint32 RequiredVertexCount, 
        uint32 RequiredIndexCount, uint32& OutPageId);

private:
    //기본 용량. 이보다 큰 메시는 요청 크기로 페이지 생성.
    static constexpr uint32 DefaultVertexCapacity = 65536;
    static constexpr uint32 DefaultIndexCapacity = 196608;

    Microsoft::WRL::ComPtr<ID3D11Device> Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;

    // 배열 인덱스가 MeshPageId.
    // 실행 중 삭제하거나 정렬하지 않는다.
    TArray<FMeshBufferPage> Pages;
};