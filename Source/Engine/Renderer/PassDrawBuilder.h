#pragma once

#include "PreparedDraw.h"
#include "PipelineStateCache.h"
#include <unordered_map>
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"

struct FFastMatrix;

class FPassDrawBuilder {
public:
	FPassDrawBuilder() = default;
	~FPassDrawBuilder() = default;

	void BuildPassDraws(const FViewRenderData& ViewLayoutData, FPipelineStateCache* PipelineCache, 
		FPassDrawList& OutPassDraws);
	const TArray<uint32>& GetObjectCBIndexMap() const { return ObjectIndexToCBIndex; }
	const TArray<uint32>& GetReferenceObjectIndices() const { return ReferenceObjectIndices; }
	const TMap<uint32, uint32>& GetMaterialCBIndexMap() const { return m_MaterialIdToCBIndex; }
	const TArray<const FMaterial*>& GetReferencedMaterials() const { return m_ReferencedMaterials; }
	// 현재 View에서 사용한 머티리얼의 비소유 참조를 해제합니다.
	void ReleaseViewReferences()
	{
		// 다음 View의 매핑 초기화는 기존 BuildPassDraws 경로에서 수행합니다.
		m_ReferencedMaterials.Empty();
	}
private:
	FPipelineKey MakePipelineKey(const FMaterial* Material, EVertexFormat VertexFormat, EPipelinePass Pass, EViewModeIndex ViewMode) const;
	uint32 CalculateDepthBucket(const FVector& SortCenterWS, const FFastMatrix& ViewMatrix, float NearZ = 0.1f, float FarZ = 1000.0f) const;

private:
	void ResetObjectMapping(size_t ObjectCount);
	uint32 GetOrCreateObjectCBIndex(uint32 ObjectIndex);
	TArray<uint32> ObjectIndexToCBIndex;
	TArray<uint32> ReferenceObjectIndices;
	TMap<uint32, uint32> m_MaterialIdToCBIndex;
	TArray<const FMaterial*> m_ReferencedMaterials;
};
