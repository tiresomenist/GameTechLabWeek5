#pragma once

#include <d3d11.h>
#include <wrl/client.h>

class GContext
{
public:
    static GContext* GetInstance();

    void Initialize(ID3D11Device* InDevice);
    void Release();

    bool IsInitialized() const;

    // 소유권을 넘기지 않음. 호출자가 Release하면 안 됨.
    ID3D11DeviceContext* GetNative() const;

    void ClearState();
    void Flush();

    void UnbindRenderTargets();

    void SetRenderTargets(
        ID3D11RenderTargetView* RTV,
        ID3D11DepthStencilView* DSV);

    void SetViewport(const D3D11_VIEWPORT& Viewport);

    void DrawIndexed(UINT IndexCount,UINT StartIndexLocation,INT BaseVertexLocation = 0);

private:
    GContext();
    ~GContext();

    GContext(const GContext&) = delete;
    GContext& operator=(const GContext&) = delete;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
};