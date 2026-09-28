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
};

struct FPassDrawList {
	std::vector<FPreparedDraw> OpaqueDraws;
	std::vector<FPreparedDraw> AdditiveDraws;
	std::vector<FPreparedDraw> OutlineDraws;
	std::vector<FPreparedDraw> GizmoDraws;

	void Clear() {
		OpaqueDraws.clear();
		AdditiveDraws.clear();
		OutlineDraws.clear();
		GizmoDraws.clear();
	}
};