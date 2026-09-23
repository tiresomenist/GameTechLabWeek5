#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include "Core/Core.h"

struct FVertexSimple;
struct FVertexTest;

// Device 생성 → Renderer/ResourceManager 생성 → (역순으로) Renderer/ResourceManager 소멸 → Device 소멸

class GDevice
{
public:
    static GDevice* GetInstance();

    void Initialize();
    void Release();
    

    Microsoft::WRL::ComPtr<ID3D11Buffer> CreateVertexBuffer(const void* VertexData, UINT ByteWidth);

    Microsoft::WRL::ComPtr<ID3D11Buffer> CreateIndexBuffer(const uint32* Indices, UINT ByteWidth);

    // 모두 빌린 참조를 반환한다.
    ID3D11Device* GetDevice() const;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> Device = nullptr;

    GDevice() = default;
    ~GDevice() = default;
    GDevice(const GDevice&) = delete;
    GDevice& operator=(const GDevice&) = delete;
};
