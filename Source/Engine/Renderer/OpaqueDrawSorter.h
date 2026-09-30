#pragma once
#include "PreparedDraw.h"
#include <vector>

class FOpaqueDrawSorter {
public:
	//PipelineId->MaterialId->DepthBucket->MeshPageId 순으로 정렬
	static void SortOpaqueDraws(
		TArray<FPreparedDraw>& Draws,
		TArray<FPreparedDraw>& DrawScratch,
		TArray<uint32>& IndexScratchA,
		TArray<uint32>& IndexScratchB);

	bool IsSorted(const TArray<FPreparedDraw>& Draws);
};