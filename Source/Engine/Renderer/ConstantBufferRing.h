#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <cassert>

using Microsoft::WRL::ComPtr;

struct FCBRangeAllocation {
	ID3D11Buffer* Buffer = nullptr;
	uint32_t ByteOffset = 0;
	uint32_t ByteSize = 0;

	UINT FirstConstant = 0;
	UINT NumConstants = 0;

	bool IsValid() const { return Buffer != nullptr; }
};

class FConstantBufferRing {
public:
	FConstantBufferRing() = default;
	~FConstantBufferRing() = default;

	FConstantBufferRing(const FConstantBufferRing&) = default;
	FConstantBufferRing& operator=(const FConstantBufferRing&) = default;

	bool Initialize(ID3D11Device* Device, uint32_t TotalByteSize = 2 * 1024 * 1024);
	void Shutdown();

	void Reset();

	FCBRangeAllocation AllocateAndUpload(ID3D11DeviceContext* Context, const void* Data, uint32_t DataByteSize);
	bool BeginFrameMap(ID3D11DeviceContext* Context);
	FCBRangeAllocation AllocateFast(const void* Data, uint32_t DataByteSize);
	void EndFrameMap(ID3D11DeviceContext* Context);

	bool AllocateFastBatch(uint32_t DataByteSize, uint32_t Count, uint32_t& OutBaseOffset, uint32_t& OutAlignedSize);

	uint8_t* GetMappedWritePtr(uint32_t ByteOffset)
	{
		return m_pMappedData + ByteOffset;
	}

	ID3D11Buffer* GetBuffer() const
	{
		return m_Buffer.Get();
	}

public:
	static constexpr uint32_t CB_ALIGNMENT = 256;
	static uint32_t AlignUp(uint32_t Size, uint32_t Alignment) {
		return (Size + (Alignment - 1)) & ~(Alignment - 1);
	}
private:
	ComPtr<ID3D11Buffer> m_Buffer;
	uint32_t m_TotalCapacity = 0;
	uint32_t m_CurrentOffset = 0;
	bool m_bIsFirstAllocationInFrame = true;
	uint8_t* m_pMappedData = nullptr;
};