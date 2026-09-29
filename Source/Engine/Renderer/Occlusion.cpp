#include "pch.h"
#include "Core/Core.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Occlusion.h"

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

void FHZBOcclusionCuller::QueueReadback(ID3D11DeviceContext* Context, const TArray<uint64>& CellKeys)
{
    if (bReadbackPending || !ReadbackBuffer || !VisibilityBuffer) return;
    if (static_cast<uint32>(CellKeys.Num()) != CellCount) return;

    PendingCellKeys.SetNum(CellKeys.Num());
    for (int32 Index = 0; Index < CellKeys.Num(); ++Index)
    {
        PendingCellKeys[Index] = CellKeys[Index];
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
