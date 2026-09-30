#include "pch.h"
#include "ConstantBufferManager.h"
#include <cassert>
void FConstantBufferManager::UploadObjectConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const FViewRenderData& ViewLayoutData, const TArray<uint32>& ObjectIndexToCBIndices)
{
	const uint32 ObjectCount =
		static_cast<uint32>(ObjectIndexToCBIndices.Num());

	m_ObjectCBAllocations.SetNum(ObjectCount);

	if (ObjectCount == 0)
		return;

	const TArray<FRenderObjectData>& Objects =
		ViewLayoutData.Objects;

	const FFastMatrix& ViewProj =
		ViewLayoutData.View.ViewProjection;

	const uint32 DataSize =
		static_cast<uint32>(sizeof(FObjectConstants));

	uint32 BaseOffset = 0;
	uint32 AlignedSize = 0;

	if (!CBRing->AllocateFastBatch(
		DataSize,
		ObjectCount,
		BaseOffset,
		AlignedSize))
	{
		return;
	}

	for (uint32 CBIndex = 0;
		CBIndex < ObjectCount;
		++CBIndex)
	{
		const uint32 ObjectIndex =
			ObjectIndexToCBIndices[CBIndex];

		const FRenderObjectData& Object =
			Objects[ObjectIndex];

		FObjectConstants Constants;

		Constants.World = Object.World;
		Constants.ViewProjection = ViewProj;

		Constants.ImpostorCenterWS =
			FVector4(
				Object.ImpostorCenterWS.X,
				Object.ImpostorCenterWS.Y,
				Object.ImpostorCenterWS.Z,
				1.0f);

		Constants.ImpostorSize =
			Object.ImpostorSize;

		Constants.ImpostorUV =
			Object.ImpostorUV;

		Constants.ImpostorCameraLocation =
			FVector4(
				Object.ImpostorCameraLocation.X,
				Object.ImpostorCameraLocation.Y,
				Object.ImpostorCameraLocation.Z,
				1.0f);

		const uint32 ByteOffset =
			BaseOffset + CBIndex * AlignedSize;

		uint8_t* WritePtr =
			CBRing->GetMappedWritePtr(ByteOffset);

		std::memcpy(
			WritePtr,
			&Constants,
			DataSize);

		if (AlignedSize > DataSize)
		{
			std::memset(WritePtr + DataSize, 0, AlignedSize - DataSize);
		}

		FCBRangeAllocation& Alloc = m_ObjectCBAllocations[CBIndex];

		Alloc.Buffer =
			CBRing->GetBuffer();

		Alloc.ByteOffset = ByteOffset;

		Alloc.ByteSize = AlignedSize;

		Alloc.FirstConstant = ByteOffset / 16;

		Alloc.NumConstants = AlignedSize / 16;
	}
}

void FConstantBufferManager::UploadMaterialConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const TArray<const FMaterial*>& ReferencedMaterials)
{
	m_MaterialCBAllocations.SetNum(ReferencedMaterials.Num());

	for (int32 CBIndex = 0; CBIndex < ReferencedMaterials.Num(); ++CBIndex)
	{
		const FMaterial& Material = *ReferencedMaterials[CBIndex];

		FTextureDrawConstants Constants{};
		Constants.DiffuseColor = Material.DiffuseColor;
		Constants.AlphaCutoff = Material.AlphaCutoff;
		Constants.UV.Scale = Material.UVScale;
		Constants.UV.Offset = Material.UVOffset;

		// Tick에서 갱신된 UVOffset을 이번 View의 상수에 반영한다.
		m_MaterialCBAllocations[CBIndex] = CBRing->AllocateAndUpload(Context, &Constants, sizeof(Constants));
	}
}

const FCBRangeAllocation& FConstantBufferManager::GetObjectCBRange(uint32 ObjectCBIndex) const
{
	return m_ObjectCBAllocations[ObjectCBIndex];
}

const FCBRangeAllocation& FConstantBufferManager::GetMaterialCBRange(uint32 MaterialCBIndex) const
{
	return m_MaterialCBAllocations[MaterialCBIndex];
}

void FConstantBufferManager::Clear()
{
	m_ObjectCBAllocations.Empty();
	m_MaterialCBAllocations.Empty();
}
