#pragma once

#include "ConstantBufferRing.h"
#include "RenderConstants.h"
#include "Engine/Renderer/ViewRenderData.h"
#include "Engine/Renderer/Material.h"
#include <vector>

class FConstantBufferManager {
public:
	FConstantBufferManager() = default;
	~FConstantBufferManager() = default;

	void UploadObjectConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const FViewRenderData& ViewLayoutData, const std::unordered_map<uint32, uint32>& ObjectIndexToCBIndexMap);

	void UploadMaterialConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const std::vector<const FMaterial*>& ReferencedMaterials, const std::unordered_map<uint32, uint32>& MaterialIdToCBIndexMap);
	
	const FCBRangeAllocation& GetObjectCBRange(uint32 ObjectCBIndex) const;
	const FCBRangeAllocation& GetMaterialCBRange(uint32 MaterialCBIndex) const;

	void Clear();
private:
	std::vector<FCBRangeAllocation> m_ObjectCBAllocations;
	std::vector<FCBRangeAllocation> m_MaterialCBAllocations;
};