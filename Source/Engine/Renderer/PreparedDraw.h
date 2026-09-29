#pragma once

#include <d3d11.h>
#include <cstdint>
#include <vector>

#include "Engine/Renderer/RenderDataTypes.h"
#include "Engine/Renderer/Material.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Renderer/ViewRenderData.h"
#include "Engine/Renderer/PipelineState.h"

struct FPreparedDraw {
	const FPrimitiveRenderData* Source = nullptr;

	uint32 PipelineId = InvalidRenderId;
	uint32 ObjectConstantIndex = InvalidRenderId;
	uint32 MaterialConstantIndex = InvalidRenderId;
	uint32 DepthBucket = 0;

	uint32 MaterialId = InvalidRenderId;
	uint32 MeshPageId = InvalidRenderId;
	uint32 ObjectIndex = InvalidRenderId;

	uint64 SortKey = 0;
};

struct FPassDrawList {
	TArray<FPreparedDraw> OpaqueDraws;
	TArray<FPreparedDraw> AdditiveDraws;
	TArray<FPreparedDraw> OutlineDraws;
	TArray<FPreparedDraw> GizmoDraws;

	void Clear() {
		OpaqueDraws.Empty();
		AdditiveDraws.Empty();
		OutlineDraws.Empty();
		GizmoDraws.Empty();
	}
};