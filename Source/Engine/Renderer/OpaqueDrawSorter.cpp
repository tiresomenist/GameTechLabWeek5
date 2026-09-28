#include "pch.h"
#include "OpaqueDrawSorter.h"

void FOpaqueDrawSorter::SortOpaqueDraws(std::vector<FPreparedDraw>& InOutOpaqueDraws)
{
	if (InOutOpaqueDraws.size() <= 1) return;

	std::sort(InOutOpaqueDraws.begin(), InOutOpaqueDraws.end(),
		[](const FPreparedDraw& A, const FPreparedDraw& B) {
		if (A.PipelineId != B.PipelineId) {
			return A.PipelineId < B.PipelineId;
		}
		if (A.MaterialId != B.MaterialId) {
			return A.MaterialId < B.MaterialId;
		}
		if (A.DepthBucket != B.DepthBucket) {
			return A.DepthBucket < B.DepthBucket;
		}
		return A.MeshPageId < B.MeshPageId;
	});
}
