#pragma once

#include "Gizmo.h"
#include "Core/Container/Array.h"
#include "Engine/Renderer/PrimitiveRenderData.h"

class UWorldAxisGizmo : public UGizmo
{

	UCLASS(UWorldAxisGizmo, "WorldAxisGizmo", UGizmo)

public:

	virtual TArray<FPrimitiveRenderData> GetRenderData(const UCameraComponent* Camera,
		const D3D11_VIEWPORT& Viewport) override;
	FLineDrawRequest BuildLineDrawRequest(const FGrid& Grid,const FVector& CameraPosition) const override;
};

