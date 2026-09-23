#include "pch.h"
#include "Engine/Renderer/Context.h"
#include <cassert>
#include <stdexcept>

GContext::GContext() = default;
GContext::~GContext() = default;


GContext* GContext::GetInstance() {
	static GContext Instance;
	return &Instance;
}

void GContext::Initialize(ID3D11Device* InDevice) {
    if (!InDevice)
    {
        throw std::invalid_argument("GContext requires a valid device");
    }

    if (IsInitialized())
    {
        throw std::logic_error("GContext is already initialized");
    }

    // Device의 기존 Immediate Context를 가져온다.
    // 반환받은 COM 참조는 Context가 소유한다.
    InDevice->GetImmediateContext(Context.GetAddressOf());
}
void GContext::Release() {
    ClearState();
    Flush();
    Context.Reset();
}

bool GContext::IsInitialized() const {
    return Context.Get() != nullptr;
}

// 소유권을 넘기지 않음. 호출자가 Release하면 안 됨.
ID3D11DeviceContext* GContext::GetNative() const {
    return Context.Get();
}

void GContext::ClearState() {
    if (IsInitialized()) { Context->ClearState(); }
}
void GContext::Flush() {
    if (IsInitialized()) { Context->Flush(); }
}

void GContext::UnbindRenderTargets() {
    assert(IsInitialized());
    Context->OMSetRenderTargets(0, nullptr, nullptr);
}

void GContext::SetRenderTargets(ID3D11RenderTargetView* RTV, ID3D11DepthStencilView* DSV) {
    assert(IsInitialized());
    Context->OMSetRenderTargets(1, &RTV, DSV);
}

void GContext::SetViewport(const D3D11_VIEWPORT& Viewport) {
    assert(IsInitialized());

    Context->RSSetViewports(1, &Viewport);
}

void GContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    assert(IsInitialized());

    Context->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}