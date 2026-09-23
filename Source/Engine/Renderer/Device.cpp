#include "pch.h"
#include "Device.h"
#include "Engine/Log.h"
#include "Engine/Renderer/Context.h"
#include <wrl/client.h>
#include <stdexcept>
#include <utility>

using Microsoft::WRL::ComPtr;

GDevice* GDevice::GetInstance()
{
    static GDevice Instance{};
    return &Instance;
}

void GDevice::Initialize()
{
    GContext& Context = *GContext::GetInstance();

    if (Device.Get() || Context.IsInitialized())
    {
        throw std::logic_error("Graphics device is already initialized");
    }

    try
    {
        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

        UINT CreateDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG) || defined(DEBUG)
        CreateDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        ComPtr<ID3D11Device> NewDevice;

        //Device만 생성
        const HRESULT Result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
            nullptr, CreateDeviceFlags, FeatureLevels, ARRAYSIZE(FeatureLevels),
            D3D11_SDK_VERSION, NewDevice.GetAddressOf(), nullptr, nullptr);

        if (FAILED(Result))
        {
            throw std::runtime_error("Failed to create graphics device");
        }

        Device = std::move(NewDevice);
        Context.Initialize(Device.Get());
    }
    catch (...)
    {
        Release();
        throw;
    }
    
}

ID3D11Device* GDevice::GetDevice() const
{
    return Device.Get();
}


void GDevice::Release()
{
    ComPtr<ID3D11Debug> Debug;
    if (Device.Get())
    {
        // Debug 인터페이스가 없는 경우에도 종료.
        Device.As(&Debug);
    }
    
    GContext& Context = *GContext::GetInstance();

    Context.ClearState();

    Context.Release();
    Device.Reset();

    if (Debug.Get())
    {
        Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
    }
}

//const void를 사용함으로써 FVertex 종류가 달라져도 호환가능하게됨.
//구분은 렌더러의 stride,InputLayout으로 가능
//해당 버퍼들의 보유는 호출한 메쉬 리소스가 가져갑니다.
ComPtr<ID3D11Buffer> GDevice::CreateVertexBuffer(const void* VertexData, UINT ByteWidth)
{
    if (!Device.Get() || !VertexData || ByteWidth == 0)
    {
        return {};
    }
    D3D11_BUFFER_DESC vertexbufferdesc = {};
    vertexbufferdesc.ByteWidth = ByteWidth;
    vertexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexbufferSRD{};
    vertexbufferSRD.pSysMem = VertexData;

    ComPtr<ID3D11Buffer> vertexBuffer;

    HRESULT hr = Device->CreateBuffer(&vertexbufferdesc, &vertexbufferSRD, &vertexBuffer);

    if (FAILED(hr))
    {
        UE_LOG("[GDevice] Failed to create Vertex Buffer. HRESULT: {}\n", hr);
        return {};
    }
    return vertexBuffer;
};

ComPtr<ID3D11Buffer> GDevice::CreateIndexBuffer(const uint32* indices, UINT byteWidth)
{
    if (!Device.Get() || !indices || byteWidth == 0)
    {
        return {};
    }
    ComPtr<ID3D11Buffer> indexBuffer = {};

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = byteWidth;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = indices;

    HRESULT hr = Device->CreateBuffer(&bd, &initData, &indexBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[GDevice] Failed to create Index Buffer. HRESULT: {}\n", hr);
        return {};
    }

    return indexBuffer;
}