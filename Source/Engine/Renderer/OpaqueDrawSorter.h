#pragma once
#include "PreparedDraw.h"
#include <vector>

class FOpaqueDrawSorter {
public:
	//PipelineId->MaterialId->DepthBucket->MeshPageId 순으로 정렬
	static void SortOpaqueDraws(std::vector<FPreparedDraw>& InOutOpaqueDraws);
	bool IsSorted(const std::vector<FPreparedDraw>& Draws);
};