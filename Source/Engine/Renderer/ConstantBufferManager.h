#pragma once

#include "ConstantBufferRing.h"
#include "RenderConstants.h"
#include "Engine/Renderer/ViewRenderData.h"
#include "Engine/Renderer/Material.h"
#include <vector>
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"

class FConstantBufferManager {
public:
	FConstantBufferManager() = default;
	~FConstantBufferManager() = default;

	void UploadObjectConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const FViewRenderData& ViewLayoutData, const TArray<uint32>& ObjectIndexToCBIndexMap);

	void UploadMaterialConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const TArray<const FMaterial*>& ReferencedMaterials, const TMap<uint32, uint32>& MaterialIdToCBIndexMap);
	
	const FCBRangeAllocation& GetObjectCBRange(uint32 ObjectCBIndex) const;
	const FCBRangeAllocation& GetMaterialCBRange(uint32 MaterialCBIndex) const;

	void Clear();
private:
	TArray<FCBRangeAllocation> m_ObjectCBAllocations;
	TArray<FCBRangeAllocation> m_MaterialCBAllocations;
};