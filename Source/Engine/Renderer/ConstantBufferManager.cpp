#include "pch.h"
#include "ConstantBufferManager.h"
#include <cassert>
void FConstantBufferManager::UploadObjectConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const FViewRenderData& ViewLayoutData, const TArray<uint32>& ObjectIndexToCBIndexMap)
{
	m_ObjectCBAllocations.SetNum(ObjectIndexToCBIndexMap.Num());

	const auto& Objects = ViewLayoutData.Objects;
	const FMatrix& ViewProj = ViewLayoutData.View.ViewProjection;

	for (int32 CBIndex = 0; CBIndex < ObjectIndexToCBIndexMap.Num(); ++CBIndex)
	{
		const uint32 ObjectIndex = ObjectIndexToCBIndexMap[CBIndex];

		FObjectConstants Constants{};
		Constants.World = Objects[ObjectIndex].World;
		Constants.ViewProjection = ViewProj;

		// 드로우 요청의 ObjectConstantIndex와 같은 위치에 기록한다.
		m_ObjectCBAllocations[CBIndex] = CBRing->AllocateAndUpload(
			Context, &Constants, sizeof(Constants));
	}
}

void FConstantBufferManager::UploadMaterialConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const TArray<const FMaterial*>& ReferencedMaterials, const TMap<uint32, uint32>& MaterialIdToCBIndexMap)
{
	if (MaterialIdToCBIndexMap.IsEmpty()) return;

	m_MaterialCBAllocations.resize(MaterialIdToCBIndexMap.Num());

	for (const FMaterial* Material : ReferencedMaterials)
	{
		uint32 MatId = Material ? Material->MaterialId : InvalidRenderId;
		auto It = MaterialIdToCBIndexMap.Find(MatId);
		if (It == nullptr) continue;

		uint32 CBIndex = *It;

		FTextureDrawConstants MatCBData{};
		if (Material)
		{
			MatCBData.DiffuseColor = Material->DiffuseColor;
			MatCBData.AlphaCutoff = Material->AlphaCutoff;
			MatCBData.UV.Scale = Material->UVScale;
			MatCBData.UV.Offset = Material->UVOffset;
		}

		FCBRangeAllocation Alloc = CBRing->AllocateAndUpload(Context, &MatCBData, sizeof(FTextureDrawConstants));
		m_MaterialCBAllocations[CBIndex] = Alloc;
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
