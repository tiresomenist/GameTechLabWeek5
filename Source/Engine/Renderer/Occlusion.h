#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Container/Map.h"
#include "Core/Container/Array.h"
#include "Core/Math/Box.h"
#include "Engine/Renderer/PrimitiveRenderData.h"

struct FOcclusionState
{
	Microsoft::WRL::ComPtr<ID3D11Query> Query;
	bool bVisibleLastFrame = true;
	bool bQueryPending = false;  // GPU에서 결과 계산중인지
};

struct FOcclusionCell
{
	uint64 Key = 0;
	FBoundingBox Bounds;
	TArray<const FPrimitiveRenderData*> Items;
};

class FOcclusionCuller
{
public:
	bool CreateProxyMesh(ID3D11Device* Device);
	FOcclusionState& Create(ID3D11Device* Device, uint64 CellKey);

	void UpdateQueryResults(ID3D11DeviceContext* Context);
	bool VisibleLastFrame(uint64 CellKey) const;
	void Clear();

	ID3D11Buffer* GetProxyVertexBuffer() const { return ProxyVertexBuffer.Get(); }
	ID3D11Buffer* GetProxyIndexBuffer() const { return ProxyIndexBuffer.Get(); }

private:
	TMap<uint64, FOcclusionState> States;

	Microsoft::WRL::ComPtr<ID3D11Buffer> ProxyVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> ProxyIndexBuffer;
};