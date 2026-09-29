#include "pch.h"
#include "Core/Core.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Occlusion.h"

bool FOcclusionCuller::CreateProxyMesh(ID3D11Device* Device)
{
    if (!Device)
    {
        return false;
    }

    const FVertexSimple Vertices[] =
    {
        { -1, -1, -1, 1, 1, 1, 1 }, // 0
        { 1, -1, -1, 1, 1, 1, 1 }, // 1
        { 1,  1, -1, 1, 1, 1, 1 }, // 2
        { -1,  1, -1, 1, 1, 1, 1 }, // 3
        { -1, -1,  1, 1, 1, 1, 1 }, // 4
        { 1, -1,  1, 1, 1, 1, 1 }, // 5
        { 1,  1,  1, 1, 1, 1, 1 }, // 6
        { -1,  1,  1, 1, 1, 1, 1 }, // 7
    };

    const uint32 Indices[] =
    {
        0, 2, 1,  0, 3, 2, // -Z
        4, 5, 6,  4, 6, 7, // +Z
        0, 1, 5,  0, 5, 4, // -Y
        2, 3, 7,  2, 7, 6, // +Y
        0, 4, 7,  0, 7, 3, // -X
        1, 2, 6,  1, 6, 5, // +X
    };

    D3D11_BUFFER_DESC VertexDesc{};
    VertexDesc.ByteWidth = sizeof(Vertices);
    VertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    VertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA VertexData{};
    VertexData.pSysMem = Vertices;
    const HRESULT VertexResult = Device->CreateBuffer(&VertexDesc, &VertexData, ProxyVertexBuffer.GetAddressOf());
    if (FAILED(VertexResult))
    {
        return false;
    }

    D3D11_BUFFER_DESC IndexDesc{};
    IndexDesc.ByteWidth = sizeof(Indices);
    IndexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    IndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA IndexData{};
    IndexData.pSysMem = Indices;
    const HRESULT IndexResult = Device->CreateBuffer(&IndexDesc, &IndexData, ProxyIndexBuffer.GetAddressOf());
    if (FAILED(IndexResult))
    {
        ProxyVertexBuffer.Reset();
        return false;
    }

    return true;
}


FOcclusionState& FOcclusionCuller::Create(ID3D11Device* Device, uint64 CellKey)
{
    FOcclusionState& State = States[CellKey];

    // 이미 이 Primitive의 query가 만들어졌다면 재사용
    if (State.Query)
    {
        return State;
    }

    D3D11_QUERY_DESC Desc{};
    Desc.Query = D3D11_QUERY_OCCLUSION;
    Desc.MiscFlags = 0;

    const HRESULT Result = Device->CreateQuery(&Desc, State.Query.GetAddressOf());

    if (FAILED(Result))
    {
        State.bVisibleLastFrame = true;
        State.bQueryPending = false;
    }

    return State;
}

void FOcclusionCuller::UpdateQueryResults(ID3D11DeviceContext* Context)
{
    for (auto& Pair : States)
    {
        FOcclusionState& State = Pair.second;

        if (!State.Query || !State.bQueryPending)
        {
            continue;
        }

        UINT64 PixelCount = 0;

        const HRESULT Result = Context->GetData(
            State.Query.Get(),
            &PixelCount,
            sizeof(PixelCount),
            D3D11_ASYNC_GETDATA_DONOTFLUSH);

        if (Result == S_OK)
        {
            State.bVisibleLastFrame = (PixelCount > 0);
            State.bQueryPending = false;
        }
        else if (FAILED(Result))
        {
            State.bVisibleLastFrame = true;
            State.bQueryPending = false;
        }
    }
}

bool FOcclusionCuller::VisibleLastFrame(uint64 CellKey) const
{
    const FOcclusionState* State = States.Find(CellKey);

    if (State == nullptr || !State->Query)
    {
        return true;
    }

    return State->bVisibleLastFrame;
}

void FOcclusionCuller::Clear()
{
    States.Empty();
}

// FHZBOcclusionCuller
bool FHZBOcclusionCuller::UploadCells(ID3D11Device* Device, ID3D11DeviceContext* Context, const TArray<FHZBCellData>& Cells)
{
    CellCount = static_cast<uint32>(Cells.Num());
    if (CellCount == 0) { return true; }

    if (!CellBuffer || !CellSRV || !VisibilityBuffer || !VisibilityUAV || !ReadbackBuffer || CellCapacity < CellCount)
    {
        CellBuffer.Reset();
        CellSRV.Reset();
        VisibilityBuffer.Reset();
        ReadbackBuffer.Reset();
        bReadbackPending = false;
        PendingCellKeys.Empty();
        VisibilityUAV.Reset();
        CellCapacity = 0;
        LastFrameVisibility.Empty();

        D3D11_BUFFER_DESC BufferDesc{};
        BufferDesc.ByteWidth = CellCount * sizeof(FHZBCellData);
        BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; 
        BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        BufferDesc.StructureByteStride = sizeof(FHZBCellData);
        if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, CellBuffer.GetAddressOf())))
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
        SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.FirstElement = 0;
        SRVDesc.Buffer.NumElements = CellCount;
        if (FAILED(Device->CreateShaderResourceView(CellBuffer.Get(), &SRVDesc, CellSRV.GetAddressOf())))
        {
            CellBuffer.Reset();
            return false;
        }

        D3D11_BUFFER_DESC VisibilityDesc{};
        VisibilityDesc.ByteWidth = CellCount * sizeof(uint32);
        VisibilityDesc.Usage = D3D11_USAGE_DEFAULT;
        VisibilityDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        VisibilityDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        VisibilityDesc.StructureByteStride = sizeof(uint32);
        if (FAILED(Device->CreateBuffer(&VisibilityDesc, nullptr, VisibilityBuffer.GetAddressOf())))
        {
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC VisibilityUAVDesc{};
        VisibilityUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
        VisibilityUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        VisibilityUAVDesc.Buffer.FirstElement = 0;
        VisibilityUAVDesc.Buffer.NumElements = CellCount;
        if (FAILED(Device->CreateUnorderedAccessView(VisibilityBuffer.Get(), &VisibilityUAVDesc, VisibilityUAV.GetAddressOf())))
        {
            return false;
        }

        D3D11_BUFFER_DESC ReadbackDesc{};
        ReadbackDesc.ByteWidth = CellCount * sizeof(uint32);
        ReadbackDesc.Usage = D3D11_USAGE_STAGING;
        ReadbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(Device->CreateBuffer(&ReadbackDesc, nullptr, ReadbackBuffer.GetAddressOf())))
        {
            return false;
        }

        CellCapacity = CellCount;
    }

    D3D11_MAPPED_SUBRESOURCE Mapped{};
    if (FAILED(Context->Map(CellBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return false;
    }
    memcpy(Mapped.pData, Cells.GetData(), CellCount * sizeof(FHZBCellData));
    Context->Unmap(CellBuffer.Get(), 0);
    return true;
}

void FHZBOcclusionCuller::Release()
{
    CellBuffer.Reset();
    CellSRV.Reset();
    VisibilityBuffer.Reset();
    ReadbackBuffer.Reset();
    VisibilityUAV.Reset();
    CellCount = 0;
    CellCapacity = 0;

    bReadbackPending = false;
    PendingCellKeys.Empty();
    LastFrameVisibility.Empty();
}

void FHZBOcclusionCuller::QueueReadback(ID3D11DeviceContext* Context, const TArray<FOcclusionCell>& Cells)
{
    if (bReadbackPending || !ReadbackBuffer || !VisibilityBuffer) return;
    if (static_cast<uint32>(Cells.Num()) != CellCount) return;

    PendingCellKeys.SetNum(Cells.Num());
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
    {
        PendingCellKeys[Index] = Cells[Index].Key;
    }
    Context->CopyResource(ReadbackBuffer.Get(), VisibilityBuffer.Get());
    bReadbackPending = true;
}

bool FHZBOcclusionCuller::TryReadback(ID3D11DeviceContext* Context)
{
    if (!bReadbackPending||!ReadbackBuffer)
    {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE Mapped{};
    const HRESULT Result = Context->Map(ReadbackBuffer.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &Mapped);
    if (Result == DXGI_ERROR_WAS_STILL_DRAWING) return false;
    if (FAILED(Result))
    {
        Release();
        return false;
    }

    const uint32* Visibility = static_cast<const uint32*>(Mapped.pData);
    LastFrameVisibility.Empty();
    for (int32 Index = 0; Index < PendingCellKeys.Num(); ++Index)
    {
        LastFrameVisibility[PendingCellKeys[Index]] = Visibility[Index] != 0;
    }
    Context->Unmap(ReadbackBuffer.Get(), 0);
    bReadbackPending = false;
    return true;
}

bool FHZBOcclusionCuller::IsVisibleLastFrame(uint64 CellKey) const
{
    const bool* bVisible = LastFrameVisibility.Find(CellKey);
    return (!bVisible || *bVisible);
}