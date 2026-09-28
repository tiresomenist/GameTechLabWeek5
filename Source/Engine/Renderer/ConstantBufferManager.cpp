#include "pch.h"
#include "ConstantBufferManager.h"
#include <cassert>
void FConstantBufferManager::UploadObjectConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const FViewRenderData& ViewLayoutData, const std::unordered_map<uint32, uint32>& ObjectIndexToCBIndexMap)
{
	if (ObjectIndexToCBIndexMap.empty()) return;

	m_ObjectCBAllocations.resize(ObjectIndexToCBIndexMap.size());

	const auto& Objects = ViewLayoutData.Objects;
	const FMatrix& ViewProj = ViewLayoutData.View.ViewProjection;

	for (const auto& [ObjIndex, CBIndex] : ObjectIndexToCBIndexMap) {
		assert(CBIndex < m_ObjectCBAllocations.size());
		assert(ObjIndex < Objects.size());

		FObjectConstants ObjCBData;
		ObjCBData.World = Objects[ObjIndex].World;
		ObjCBData.ViewProjection = ViewProj;

		FCBRangeAllocation Alloc = CBRing->AllocateAndUpload(Context, &ObjCBData, sizeof(FObjectConstants));
		m_ObjectCBAllocations[CBIndex] = Alloc;
	}
}

void FConstantBufferManager::UploadMaterialConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const std::vector<const FMaterial*>& ReferencedMaterials, const std::unordered_map<uint32, uint32>& MaterialIdToCBIndexMap)
{
	if (MaterialIdToCBIndexMap.empty()) return;

	m_MaterialCBAllocations.resize(MaterialIdToCBIndexMap.size());

	for (const FMaterial* Material : ReferencedMaterials)
	{
		uint32 MatId = Material ? Material->MaterialId : InvalidRenderId;
		auto It = MaterialIdToCBIndexMap.find(MatId);
		if (It == MaterialIdToCBIndexMap.end()) continue;

		uint32 CBIndex = It->second;
		assert(CBIndex < m_MaterialCBAllocations.size());

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
	assert(ObjectCBIndex < m_ObjectCBAllocations.size());
	return m_ObjectCBAllocations[ObjectCBIndex];
}

const FCBRangeAllocation& FConstantBufferManager::GetMaterialCBRange(uint32 MaterialCBIndex) const
{
	assert(MaterialCBIndex < m_MaterialCBAllocations.size());
	return m_MaterialCBAllocations[MaterialCBIndex];
}

void FConstantBufferManager::Clear()
{
	m_ObjectCBAllocations.clear();
	m_MaterialCBAllocations.clear();
}
