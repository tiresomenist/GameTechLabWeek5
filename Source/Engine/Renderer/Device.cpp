#include "pch.h"
#include "Device.h"
#include "Engine/Log.h"
#include "Engine/Renderer/Context.h"
#include <wrl/client.h>
#include <stdexcept>
#include <utility>
#include <dxgi1_6.h>
#pragma comment(lib,"dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // 고성능 하드웨어 GPU부터 Device 생성을 시도하고, 실패하면 기본 GPU를 사용합니다.
    HRESULT CreatePreferredHardwareDevice(UINT Flags, ID3D11Device** OutDevice)
    {
        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        ComPtr<IDXGIFactory6> Factory;

        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(Factory.GetAddressOf()))))
        {
            for (uint32 Index = 0; ; ++Index)
            {
                ComPtr<IDXGIAdapter1> Adapter;
                const HRESULT EnumerateResult = Factory->EnumAdapterByGpuPreference(
                    Index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(Adapter.GetAddressOf()));
                if (FAILED(EnumerateResult)) break;

                DXGI_ADAPTER_DESC1 Desc{};
                if (FAILED(Adapter->GetDesc1(&Desc))) continue;
                if ((Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;

                // 어댑터를 직접 지정할 때는 UNKNOWN을 사용해야 합니다.
                ComPtr<ID3D11Device> CandidateDevice;
                const HRESULT Result = D3D11CreateDevice(
                    Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, Flags,
                    FeatureLevels, ARRAYSIZE(FeatureLevels), D3D11_SDK_VERSION,
                    CandidateDevice.GetAddressOf(), nullptr, nullptr);

                if (SUCCEEDED(Result))
                {
                    *OutDevice = CandidateDevice.Detach();
                    return Result;
                }
            }
        }

        // 고성능 어댑터 열거 또는 생성이 실패하면 기존 생성 경로를 사용합니다.
        return D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags,
            FeatureLevels, ARRAYSIZE(FeatureLevels), D3D11_SDK_VERSION,
            OutDevice, nullptr, nullptr);
    }
    // 실제 생성된 Device의 GPU 이름을 Visual Studio 출력 창에 표시합니다.
    void PrintSelectedAdapter(ID3D11Device* Device)
    {
        if (!Device) return;

        ComPtr<IDXGIDevice> DxgiDevice;
        if (FAILED(Device->QueryInterface(IID_PPV_ARGS(DxgiDevice.GetAddressOf())))) return;

        ComPtr<IDXGIAdapter> Adapter;
        if (FAILED(DxgiDevice->GetAdapter(Adapter.GetAddressOf()))) return;

        DXGI_ADAPTER_DESC Desc{};
        if (FAILED(Adapter->GetDesc(&Desc))) return;

        // 요청한 우선순위가 아닌, 실제 Device에 연결된 GPU 이름입니다.
        OutputDebugStringW(L"[GPU] ");
        OutputDebugStringW(Desc.Description);
        OutputDebugStringW(L"\n");
    }
}

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

        UINT CreateDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG) || defined(DEBUG)
        CreateDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        ComPtr<ID3D11Device> NewDevice;

        // 사용 가능한 고성능 GPU를 우선 선택합니다.
        const HRESULT Result = CreatePreferredHardwareDevice(
            CreateDeviceFlags, NewDevice.GetAddressOf());

        if (FAILED(Result))
        {
            throw std::runtime_error("Failed to create graphics device");
        }

        Device = std::move(NewDevice);
        Context.Initialize(Device.Get());
        PrintSelectedAdapter(Device.Get());
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