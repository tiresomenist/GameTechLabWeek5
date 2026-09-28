#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Container/Map.h"

class UPrimitiveComponent;

struct FOcclusionState
{
	Microsoft::WRL::ComPtr<ID3D11Query> Query;
	bool bVisibleLastFrame = true;
	bool bQueryPending = false;  // GPU에서 결과 계산중인지
};

class FOcclusionCuller
{
public:
	bool CreateProxyMesh(ID3D11Device* Device);
	FOcclusionState& Create(ID3D11Device* Device, const UPrimitiveComponent* Owner);

	void UpdateQueryResults(ID3D11DeviceContext* Context);
	bool VisibleLastFrame(const UPrimitiveComponent* Owner) const;
	void Clear();

	ID3D11Buffer* GetProxyVertexBuffer() const { return ProxyVertexBuffer.Get(); }
	ID3D11Buffer* GetProxyIndexBuffer() const { return ProxyIndexBuffer.Get(); }

private:
	// Owner를 Key로 하여 지난 프레임 결과를 저장
	TMap<const UPrimitiveComponent*, FOcclusionState> States;

	Microsoft::WRL::ComPtr<ID3D11Buffer> ProxyVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> ProxyIndexBuffer;
};