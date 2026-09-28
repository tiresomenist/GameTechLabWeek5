#pragma once

#include "PreparedDraw.h"
#include "PipelineStateCache.h"
#include <unordered_map>

class FPassDrawBuilder {
public:
	FPassDrawBuilder() = default;
	~FPassDrawBuilder() = default;

	void BuildPassDraws(const FViewRenderData& ViewLayoutData, FPipelineStateCache* PipelineCache, FPassDrawList& OutPassDraws);
	const std::unordered_map<uint32, uint32>& GetObjectCBIndexMap() const { return m_ObjectIndexToCBIndex; }
	const std::unordered_map<uint32, uint32>& GetMaterialCBIndexMap() const { return m_MaterialIdToCBIndex; }
	const std::vector<const FMaterial*>& GetReferencedMaterials() const { return m_ReferencedMaterials; }
private:
	FPipelineKey MakePipelineKey(const FMaterial* Material, EVertexFormat VertexFormat, EPipelinePass Pass, EViewModeIndex ViewMode) const;
	uint32 CalculateDepthBucket(const FVector& SortCenterWS, const FMatrix& ViewMatrix, float NearZ = 0.1f, float FarZ = 1000.0f) const;

private:
	std::unordered_map<uint32, uint32> m_ObjectIndexToCBIndex;
	std::unordered_map<uint32, uint32> m_MaterialIdToCBIndex;
	std::vector<const FMaterial*> m_ReferencedMaterials;
};