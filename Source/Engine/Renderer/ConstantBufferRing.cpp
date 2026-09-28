#include "pch.h"
#include "ConstantBufferRing.h"
#include <cstring>

bool FConstantBufferRing::Initialize(ID3D11Device* Device, uint32_t TotalByteSize)
{
    Shutdown();

    m_TotalCapacity = AlignUp(TotalByteSize, CB_ALIGNMENT);

    D3D11_BUFFER_DESC Desc{};
    Desc.ByteWidth = m_TotalCapacity;
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Desc.MiscFlags = 0;
    Desc.StructureByteStride = 0;

    HRESULT hr = Device->CreateBuffer(&Desc, nullptr, m_Buffer.GetAddressOf());
    if (FAILED(hr)) {
        assert(false && "Failed to create ConstantBufferRing GPU Buffer!");
        return false;
    }

    m_CurrentOffset = 0;
    m_bIsFirstAllocationInFrame = true;

    return true;
}

void FConstantBufferRing::Shutdown()
{
    m_Buffer.Reset();
    m_TotalCapacity = 0;
    m_CurrentOffset = 0;
    m_bIsFirstAllocationInFrame = true;
}

void FConstantBufferRing::Reset()
{
    m_CurrentOffset = 0;
    m_bIsFirstAllocationInFrame = true;
}

FCBRangeAllocation FConstantBufferRing::AllocateAndUpload(ID3D11DeviceContext* Context, const void* Data, uint32_t DataByteSize)
{
    if (m_pMappedData != nullptr) {
        return AllocateFast(Data, DataByteSize);
    }

    FCBRangeAllocation Alloc{};
    if (!m_Buffer || DataByteSize == 0)  return Alloc;

    uint32_t AlignedSize = AlignUp(DataByteSize, CB_ALIGNMENT);
    assert(AlignedSize <= m_TotalCapacity && "Allocation size exceeds ConstantBufferRing capacity!");

    D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;

    if (m_bIsFirstAllocationInFrame || (m_CurrentOffset + AlignedSize > m_TotalCapacity)) {
        MapType = D3D11_MAP_WRITE_DISCARD;
        m_CurrentOffset = 0;
        m_bIsFirstAllocationInFrame = false;
    }

    D3D11_MAPPED_SUBRESOURCE MappedResource{};
    HRESULT hr = Context->Map(m_Buffer.Get(), 0, MapType, 0, &MappedResource);
    if (FAILED(hr)) {
        assert(false && "Failed to Map ConstantBufferRing!");
        return Alloc;
    }

    uint8_t* WritePtr = static_cast<uint8_t*>(MappedResource.pData) + m_CurrentOffset;
    std::memcpy(WritePtr, Data, DataByteSize);

    if (AlignedSize > DataByteSize) {
        std::memset(WritePtr + DataByteSize, 0, AlignedSize - DataByteSize);
    }

    Context->Unmap(m_Buffer.Get(), 0);

    Alloc.Buffer = m_Buffer.Get();
    Alloc.ByteOffset = m_CurrentOffset;
    Alloc.ByteSize = AlignedSize;

    Alloc.FirstConstant = m_CurrentOffset / 16;
    Alloc.NumConstants = AlignedSize / 16;

    m_CurrentOffset += AlignedSize;

    return Alloc;
}

bool FConstantBufferRing::BeginFrameMap(ID3D11DeviceContext* Context)
{
    if (!m_Buffer || !Context) return false;

    if (m_pMappedData != nullptr) return true;

    D3D11_MAPPED_SUBRESOURCE MappedResource{};
    HRESULT hr = Context->Map(m_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
    if (FAILED(hr)) {
        assert(false && "Failed to Map ConstantBufferRing in BeginFrameMap!");
        return false;
    }

    m_pMappedData = static_cast<uint8_t*>(MappedResource.pData);
    m_CurrentOffset = 0;
    m_bIsFirstAllocationInFrame = false;
    return true;
}

FCBRangeAllocation FConstantBufferRing::AllocateFast(const void* Data, uint32_t DataByteSize)
{
    FCBRangeAllocation Alloc{};
    if (!m_pMappedData || DataByteSize == 0) return Alloc;

    uint32_t AlignedSize = AlignUp(DataByteSize, CB_ALIGNMENT);

    if (m_CurrentOffset + AlignedSize > m_TotalCapacity) {
        assert(false && "ConstantBufferRing capacity exceeded! Increase ring buffer size.");
        return Alloc;
    }

    uint8_t* WritePtr = m_pMappedData + m_CurrentOffset;
    std::memcpy(WritePtr, Data, DataByteSize);

    if (AlignedSize > DataByteSize) {
        std::memset(WritePtr + DataByteSize, 0, AlignedSize - DataByteSize);
    }

    Alloc.Buffer = m_Buffer.Get();
    Alloc.ByteOffset = m_CurrentOffset;
    Alloc.ByteSize = AlignedSize;

    Alloc.FirstConstant = m_CurrentOffset / 16;
    Alloc.NumConstants = AlignedSize / 16;

    m_CurrentOffset += AlignedSize;

    return Alloc;
}

void FConstantBufferRing::EndFrameMap(ID3D11DeviceContext* Context)
{
    if (m_pMappedData && m_Buffer && Context) {
        Context->Unmap(m_Buffer.Get(), 0);
        m_pMappedData = nullptr;
    }
}
