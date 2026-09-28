#include "pch.h"
#include "MeshBufferPool.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

#include "Engine/Renderer/VertexSimple.h"

bool FMeshBufferPool::Initialize(ID3D11Device* InDevice)
{
    // 기존 페이지가 있는 상태에서 재초기화하지 않는다.
    if (!InDevice || Device.Get()){ return false; }
    Device = InDevice;
    Device->GetImmediateContext(Context.GetAddressOf());
    return true;
}

void FMeshBufferPool::Shutdown()
{
    Pages.Empty();
    Context.Reset();
    Device.Reset();
}

uint32 FMeshBufferPool::GetVertexStride(EVertexFormat VertexFormat)
{
    switch (VertexFormat)
    {
    case EVertexFormat::Simple:
        return static_cast<uint32>(sizeof(FVertexSimple));
    case EVertexFormat::Texture:
        return static_cast<uint32>(sizeof(FVertexTexture));
    case EVertexFormat::PNCT:
        return static_cast<uint32>(sizeof(FVertexPNCT));
    default:
        return 0;
    }
}

bool FMeshBufferPool::CreatePage(EVertexFormat VertexFormat, uint32 RequiredVertexCount,
    uint32 RequiredIndexCount, uint32& OutPageId)
{
    OutPageId = InvalidRenderId;

    const uint32 Stride = GetVertexStride(VertexFormat);

    if (!Device.Get() || Stride == 0){ return false; }

    FMeshBufferPage Page;
    Page.VertexFormat = VertexFormat;
    Page.Stride = Stride;

    Page.VertexCapacity = (std::max)(DefaultVertexCapacity, RequiredVertexCount);

    Page.IndexCapacity = (std::max)(DefaultIndexCapacity, RequiredIndexCount);

    const uint32 MaxBytes = (std::numeric_limits<uint32>::max)();
    const uint32 IndexStride = static_cast<uint32>(sizeof(uint32));

    // D3D11_BUFFER_DESC::ByteWidth의 곱셈 오버플로 방지.
    if (Page.VertexCapacity > MaxBytes / Stride || Page.IndexCapacity > MaxBytes / IndexStride)
    {
        return false;
    }

    if (Page.VertexCapacity > static_cast<uint32>((std::numeric_limits<int32>::max)()))
    {
        return false;
    }

    D3D11_BUFFER_DESC VertexDesc{};
    VertexDesc.ByteWidth = Page.VertexCapacity * Stride;
    VertexDesc.Usage = D3D11_USAGE_DEFAULT;
    VertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    HRESULT Result = Device->CreateBuffer(&VertexDesc, nullptr, Page.VertexBuffer.GetAddressOf());

    if (FAILED(Result)){ return false; }

    D3D11_BUFFER_DESC IndexDesc{};
    IndexDesc.ByteWidth = Page.IndexCapacity * IndexStride;
    IndexDesc.Usage = D3D11_USAGE_DEFAULT;
    IndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    Result = Device->CreateBuffer(&IndexDesc, nullptr, Page.IndexBuffer.GetAddressOf());

    if (FAILED(Result))
    {
        // 지역 변수 Page가 이미 생성된 VB도 해제한다.
        return false;
    }

    const uint32 NewPageId = static_cast<uint32>(Pages.Num());
    Pages.Add(std::move(Page));

    OutPageId = NewPageId;
    return true;
}

bool FMeshBufferPool::AllocateAndUpload(EVertexFormat VertexFormat, const void* Vertices, uint32 VertexCount,
    std::span<const uint32> Indices, FMeshAllocation& OutAllocation)
{
    // 실패 시 이전 할당 정보가 남지 않도록 초기화.
    OutAllocation = {};

    if (!Device.Get() || !Context.Get() || !Vertices || VertexCount == 0 || Indices.empty())
    {
        return false;
    }

    const uint32 Stride = GetVertexStride(VertexFormat);
    if (Stride == 0) { return false; }

    const uint32 MaxBytes = (std::numeric_limits<uint32>::max)();
    const uint32 IndexStride = static_cast<uint32>(sizeof(uint32));

    // 인덱스 개수를 uint32로 변환하기 전에 검사한다.
    if (VertexCount > MaxBytes / Stride || Indices.size() > MaxBytes / IndexStride)
    {
        return false;
    }

    const uint32 IndexCount = static_cast<uint32>(Indices.size());

    uint32 PageId = InvalidRenderId;

    // 같은 형식이며 VB와 IB 모두 공간이 남는 페이지를 찾는다.
    for (int PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
    {
        const FMeshBufferPage& Page = Pages[PageIndex];

        if (Page.VertexFormat != VertexFormat){ continue; }

        const bool bHasVertexSpace = VertexCount <= Page.VertexCapacity - Page.UsedVertexCount;

        const bool bHasIndexSpace = IndexCount <= Page.IndexCapacity - Page.UsedIndexCount;

        if (bHasVertexSpace && bHasIndexSpace)
        {
            PageId = static_cast<uint32>(PageIndex);
            break;
        }
    }

    if (PageId == InvalidRenderId)
    {
        if (!CreatePage(VertexFormat, VertexCount, IndexCount, PageId)){ return false; }
    }

    // CreatePage에서 배열이 재할당될 수 있으므로 참조는 여기서 얻는다.
    FMeshBufferPage& Page = Pages[PageId];

    const uint32 BaseVertex = Page.UsedVertexCount;
    const uint32 FirstIndex = Page.UsedIndexCount;

    // 업로드 위치는 바이트 단위.
    D3D11_BOX VertexBox{};
    VertexBox.left = BaseVertex * Page.Stride;
    VertexBox.right = (BaseVertex + VertexCount) * Page.Stride;
    VertexBox.top = 0;
    VertexBox.bottom = 1;
    VertexBox.front = 0;
    VertexBox.back = 1;

    Context->UpdateSubresource(Page.VertexBuffer.Get(), 0, &VertexBox, Vertices, 0, 0);

    D3D11_BOX IndexBox{};
    IndexBox.left = FirstIndex * IndexStride;
    IndexBox.right = (FirstIndex + IndexCount) * IndexStride;
    IndexBox.top = 0;
    IndexBox.bottom = 1;
    IndexBox.front = 0;
    IndexBox.back = 1;

    // 인덱스 값에 BaseVertex를 더하지 않는다.
    Context->UpdateSubresource(Page.IndexBuffer.Get(), 0, &IndexBox, Indices.data(), 0, 0);

    Page.UsedVertexCount += VertexCount;
    Page.UsedIndexCount += IndexCount;

    OutAllocation.MeshPageId = PageId;
    OutAllocation.BaseVertex = static_cast<int32>(BaseVertex);
    OutAllocation.FirstIndex = FirstIndex;
    OutAllocation.VertexCount = VertexCount;
    OutAllocation.IndexCount = IndexCount;

    return true;
}

FMeshPageBinding FMeshBufferPool::GetPageBinding(uint32 MeshPageId) const
{
    if (MeshPageId >= static_cast<uint32>(Pages.Num()))
    {
        assert(false && "Invalid mesh page ID");
        return {};
    }

    const FMeshBufferPage& Page = Pages[MeshPageId];

    FMeshPageBinding Binding{};
    Binding.VertexBuffer = Page.VertexBuffer.Get();
    Binding.IndexBuffer = Page.IndexBuffer.Get();
    Binding.Stride = Page.Stride;
    Binding.IndexFormat = DXGI_FORMAT_R32_UINT;
    Binding.VertexFormat = Page.VertexFormat;

    return Binding;
}