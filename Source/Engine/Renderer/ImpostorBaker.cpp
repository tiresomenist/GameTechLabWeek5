#include "pch.h"
#include "ImpostorBaker.h"

#include "Engine/Resource/StaticMesh.h"
#include "Engine/Resource/MeshResource.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Renderer/Context.h"
#include "Engine/Log.h"
#include "../../Core/Math/Quaternion.h"

#include <wincodec.h>
#include <filesystem>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace {
    struct FBakeObjectConstants {
        FMatrix World = FMatrix::Identity;
        FMatrix ViewProjection = FMatrix::Identity;
    };

    struct FBakeMaterialConstants {
        FVector4 DiffuseColor{ 1.0f,1.0f,1.0f,1.0f };
        float AlphaCutoff = 0.0f;
        float UseTexture = 0.0f;
        float Padding[2]{};
    };

    constexpr uint32 ViewCountX = 16;
    constexpr uint32 ViewCountY = 8;

    constexpr uint32 TileSize = 256;
}


bool FImpostorBaker::Bake(UStaticMesh* StaticMesh, const FString& OutputPath)
{
    if (!StaticMesh)
    {
        return false;
    }

    GResourceManager* RM = GResourceManager::GetInstance();

    if (!RM)
    {
        return false;
    }

    D3DDevice = RM->GetDevice()->GetDevice();

    DeviceContext = GContext::GetInstance()->GetNative();

    if (!D3DDevice || !DeviceContext)
    {
        return false;
    }

    const FStaticMeshLOD* LOD = StaticMesh->GetLOD(0);

    if (!LOD || !LOD->MeshResource || !LOD->bHasBounds)
    {
        return false;
    }

    const FVector Center = (LOD->BoundsMin + LOD->BoundsMax) * 0.5f;

    const FVector Extent = (LOD->BoundsMax - LOD->BoundsMin) * 0.5f;

    const float Radius = Extent.Length();

    if (!std::isfinite(Radius) || Radius <= EPSILON)
    {
        return false;
    }

    const FMatrix World = FMatrix::MakeTranslationMatrix(Center * -1.0f);

    const float Diameter = Radius * 2.0f;
    const float OrthoHeight = Diameter * 1.10f; // 만약 수치가 이상하면 2.0f 같은 고정값으로 테스트해보세요.

    const float CaptureDistance = Radius * 3.0f + 1.0f;
    const float NearZ = 0.01f;
    const float FarZ = CaptureDistance + Radius * 2.0f;

    const FMatrix Projection = MakeCaptureProjection(OrthoHeight, NearZ, FarZ);

    if (!CreateBakeTargets())
    {
        return false;
    }

    TArray<uint8_t> AtlasPixels;

    const uint32 AtlasWidth = TileSize * ViewCountX;

    const uint32 AtlasHeight = TileSize * ViewCountY;

    AtlasPixels.SetNumZeroed(static_cast<int32>(AtlasWidth * AtlasHeight * 4));

    const D3D11_VIEWPORT MainViewport{};
    UINT SavedViewportCount = 1;
    D3D11_VIEWPORT SavedViewport{};
    DeviceContext->RSGetViewports(&SavedViewportCount, &SavedViewport);

    for (uint32 Y = 0; Y < ViewCountY; ++Y)
    {
        const float PitchDegrees = 60.0f - 120.0f * (static_cast<float>(Y) / static_cast<float>(ViewCountY - 1));
        const float Pitch = PitchDegrees * PI / 180.0f;

        const float CosPitch = std::cos(Pitch);

        const float SinPitch = std::sin(Pitch);

        for (uint32 X = 0; X < ViewCountX; ++X)
        {
            const float Yaw = (2.0f * PI * static_cast<float>(X)) / static_cast<float>(ViewCountX);

            // object -> camera 방향
            const FVector ToCamera(CosPitch * std::cos(Yaw), CosPitch * std::sin(Yaw), SinPitch);

            const FVector CameraLocation = ToCamera * CaptureDistance;
            
            const FMatrix View = MakeCaptureView(CameraLocation, FVector::Zero);

            const FMatrix ViewProjection = World * View * Projection;

            if (!RenderView(StaticMesh, View, ViewProjection))
            {
                ReleaseBakeTargets();
                return false;
            }

            TArray<uint8_t> TilePixels;

            if (!ReadbackTile(TilePixels))
            {
                ReleaseBakeTargets();
                return false;
            }

            const uint32 AtlasX = X * TileSize;

            const uint32 AtlasY = Y * TileSize;

            const uint32 TileRowBytes = TileSize * 4;

            const uint32 AtlasRowBytes = AtlasWidth * 4;

            for (uint32 Row = 0; Row < TileSize; ++Row)
            {
                uint8_t* Destination = AtlasPixels.GetData() + (AtlasY + Row) * AtlasRowBytes + AtlasX * 4;
                const uint8_t* Source = TilePixels.GetData() + Row * TileRowBytes;

                for (uint32 Col = 0; Col < TileSize; ++Col)
                {
                    Destination[Col * 4 + 0] = Source[Col * 4 + 2]; // B -> R
                    Destination[Col * 4 + 1] = Source[Col * 4 + 1]; // G -> G
                    Destination[Col * 4 + 2] = Source[Col * 4 + 0]; // R -> B
                    Destination[Col * 4 + 3] = Source[Col * 4 + 3]; // A -> A
                }
            }
        }
    }

    // 원래 렌더링 상태 일부 복구.
    DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    if (SavedViewportCount > 0)
    {
        DeviceContext->RSSetViewports(1, &SavedViewport);
    }

    const bool bSaved = SavePNG(OutputPath, AtlasPixels, AtlasWidth, AtlasHeight);

    ReleaseBakeTargets();

    if (!bSaved)
    {
        return false;
    }

    return true;
}

bool FImpostorBaker::CreateBakeTargets()
{
    if (!D3DDevice) return false;
    
    {
        D3D11_TEXTURE2D_DESC Desc{};

        Desc.Width = TileWidth;
        Desc.Height = TileHeight;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_RENDER_TARGET;

        HRESULT hr = D3DDevice->CreateTexture2D(&Desc, nullptr, RenderTarget.GetAddressOf());

        if (FAILED(hr)) {
            UE_LOG("[ImpostorBaker] Failed to create render target. HRESULT: 0x{:08X}", static_cast<uint32>(hr));
            return false;
        }
    }
    {
        HRESULT hr = D3DDevice->CreateRenderTargetView(RenderTarget.Get(), nullptr, RenderTargetView.GetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }
    }
    {
        D3D11_TEXTURE2D_DESC Desc{};

        Desc.Width = TileWidth;
        Desc.Height = TileHeight;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        HRESULT hr =  D3DDevice->CreateTexture2D(&Desc, nullptr, DepthBuffer.GetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }

        hr =
            D3DDevice->CreateDepthStencilView(DepthBuffer.Get(), nullptr, DepthStencilView.GetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }
    }
    {
        D3D11_TEXTURE2D_DESC Desc{};

        Desc.Width = TileWidth;
        Desc.Height = TileHeight;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_STAGING;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        HRESULT hr = D3DDevice->CreateTexture2D(&Desc, nullptr, ReadbackTexture.GetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }
    }

    {
        D3D11_BUFFER_DESC Desc{};

        Desc.ByteWidth = sizeof(FBakeObjectConstants);
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        HRESULT hr = D3DDevice->CreateBuffer(&Desc, nullptr, ObjectConstantBuffer.GetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }
    }

    {
        D3D11_BUFFER_DESC Desc{};

        Desc.ByteWidth = sizeof(FBakeMaterialConstants);
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        HRESULT hr = D3DDevice->CreateBuffer(&Desc, nullptr, MaterialConstantBuffer.GetAddressOf());

        if (FAILED(hr))
        {
            return false;
        }
    }

    return true;
}

void FImpostorBaker::ReleaseBakeTargets()
{
    RenderTargetView.Reset();
    RenderTarget.Reset();

    DepthStencilView.Reset();
    DepthBuffer.Reset();

    ReadbackTexture.Reset();

    ObjectConstantBuffer.Reset();
    MaterialConstantBuffer.Reset();
}

bool FImpostorBaker::RenderView(UStaticMesh* StaticMesh, const FMatrix& View, const FMatrix& ViewProjection)
{
    if (!StaticMesh || !D3DDevice || !DeviceContext || !RenderTargetView || !DepthStencilView) return false;

	GResourceManager* RM = GResourceManager::GetInstance();

	if (!RM)
	{
		return false;
	}

	const FShaderResource* Shader = RM->GetShader(FName("Mesh.ImpostorBake"));

	if (!Shader || !Shader->VertexShader || !Shader->PixelShader || !Shader->InputLayout)
	{
		return false;
	}

	const FStaticMeshLOD* LOD = StaticMesh->GetLOD(0);

	if (!LOD || !LOD->MeshResource || LOD->Sections.IsEmpty())
	{
		return false;
	}

	const FMeshAllocation& Allocation = LOD->MeshResource->GetAllocation();

	const FMatrix World = FMatrix::Identity;

    const FBakeObjectConstants ObjectConstants
    {
        World,
        ViewProjection
    };

    DeviceContext->UpdateSubresource(ObjectConstantBuffer.Get(), 0, nullptr, &ObjectConstants, 0, 0);

    const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    DeviceContext->ClearRenderTargetView(RenderTargetView.Get(), ClearColor);

    DeviceContext->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	D3D11_VIEWPORT Viewport{};
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;
	Viewport.Width = static_cast<float>(TileWidth);
	Viewport.Height = static_cast<float>(TileHeight);
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;

    DeviceContext->RSSetViewports(1, &Viewport);

    D3D11_RECT Scissor{};
    Scissor.left = 0;
    Scissor.top = 0;
    Scissor.right = static_cast<LONG>(TileWidth);
    Scissor.bottom = static_cast<LONG>(TileHeight);

    DeviceContext->RSSetScissorRects(
        1,
        &Scissor
    );

    DeviceContext->OMSetRenderTargets(1, RenderTargetView.GetAddressOf(), DepthStencilView.Get());

    DeviceContext->IASetInputLayout(Shader->InputLayout.Get());

    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    DeviceContext->VSSetShader(Shader->VertexShader.Get(), nullptr, 0);

    DeviceContext->PSSetShader(Shader->PixelShader.Get(), nullptr, 0);

    ID3D11Buffer* ObjectCB = ObjectConstantBuffer.Get();

    DeviceContext->VSSetConstantBuffers(0, 1, &ObjectCB);

    DeviceContext->RSSetState(RM->GetRasterizerState(FName("Rasterizer.SolidNone")));

    DeviceContext->OMSetDepthStencilState(RM->GetDepthStencilState(FName("Depth.Default")), 0);

    DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);

    UINT Stride = static_cast<UINT>(sizeof(FVertexPNCT));

    UINT Offset = 0;

    FMeshPageBinding PageBinding = RM->GetMeshPageBinding(Allocation.MeshPageId);

    if (!PageBinding.VertexBuffer || !PageBinding.IndexBuffer)
    {
        return false;
    }

    DeviceContext->IASetVertexBuffers(0, 1, &PageBinding.VertexBuffer, &Stride, &Offset);
    
    DeviceContext->IASetIndexBuffer(PageBinding.IndexBuffer, PageBinding.IndexFormat, 0);

    for (const FMeshSection& Section : LOD->Sections)
    {
        if (Section.IndexCount == 0)
        {
            continue;
        }

        const FMaterial* Material = StaticMesh->GetMaterial(Section.MaterialIndex);

        if (!Material)
        {
            continue;
        }

        FBakeMaterialConstants MaterialConstants{};

        MaterialConstants.DiffuseColor = Material->DiffuseColor;

        MaterialConstants.AlphaCutoff = Material->AlphaCutoff;

        MaterialConstants.UseTexture = Material->SRV ? 1.0f : 0.0f;

        DeviceContext->UpdateSubresource(MaterialConstantBuffer.Get(), 0, nullptr, &MaterialConstants, 0, 0);

        ID3D11Buffer* MaterialCB = MaterialConstantBuffer.Get();

        DeviceContext->PSSetConstantBuffers(1, 1, &MaterialCB);

        ID3D11ShaderResourceView* SRV = Material->SRV;

        DeviceContext->PSSetShaderResources(0, 1, &SRV);

        ID3D11SamplerState* Sampler = Material->Sampler;

        DeviceContext->PSSetSamplers(0, 1, &Sampler);

        DeviceContext->DrawIndexed(Section.IndexCount, Allocation.FirstIndex + Section.FirstIndex, Allocation.BaseVertex);
    }

	ID3D11ShaderResourceView* NullSRV = nullptr;

	DeviceContext->PSSetShaderResources( 0, 1, &NullSRV );

	return true;
}

bool FImpostorBaker::ReadbackTile(TArray<uint8_t>& OutPixels)
{
    if (!ReadbackTexture || !RenderTarget)
    {
        return false;
    }

    DeviceContext->CopyResource(ReadbackTexture.Get(), RenderTarget.Get());

    D3D11_MAPPED_SUBRESOURCE Mapped{};

    HRESULT Result = DeviceContext->Map(ReadbackTexture.Get(), 0, D3D11_MAP_READ, 0, &Mapped);

    if (FAILED(Result))
    {
        return false;
    }

    const uint32 RowBytes = TileWidth * 4;

    OutPixels.SetNum(static_cast<int32>(RowBytes * TileHeight));

    const uint8_t* Source = static_cast<const uint8_t*>(Mapped.pData);

    for (uint32 Y = 0; Y < TileHeight; ++Y)
    {
        std::memcpy(OutPixels.GetData() + Y * RowBytes, Source + Y * Mapped.RowPitch, RowBytes);
    }

    DeviceContext->Unmap(ReadbackTexture.Get(), 0);

    return true;
}

bool FImpostorBaker::SavePNG(
    const FString& FilePath,
    const TArray<uint8_t>& Pixels,
    uint32 Width,
    uint32 Height)
{
    HRESULT COMResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (FAILED(COMResult) &&
        COMResult != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    const bool bNeedUninitialize =
        SUCCEEDED(COMResult);

    struct FCOMScope
    {
        bool bNeedUninitialize = false;

        ~FCOMScope()
        {
            if (bNeedUninitialize)
            {
                CoUninitialize();
            }
        }
    }
    COMScope{ bNeedUninitialize };

    if (Pixels.IsEmpty())
    {
        return false;
    }

    std::filesystem::path OutputPath(FilePath);

    if (OutputPath.has_parent_path())
    {
        std::error_code Error;

        std::filesystem::create_directories(
            OutputPath.parent_path(),
            Error
        );

        if (Error)
        {
            return false;
        }
    }

    ComPtr<IWICImagingFactory> Factory;

    HRESULT Result =
        CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(Factory.GetAddressOf())
        );

    if (FAILED(Result))
    {
        return false;
    }

    ComPtr<IWICStream> Stream;

    Result =
        Factory->CreateStream(
            Stream.GetAddressOf()
        );

    if (FAILED(Result))
    {
        return false;
    }

    const std::wstring WidePath =
        OutputPath.wstring();

    Result =
        Stream->InitializeFromFilename(
            WidePath.c_str(),
            GENERIC_WRITE
        );

    if (FAILED(Result))
    {
        return false;
    }

    ComPtr<IWICBitmapEncoder> Encoder;

    Result =
        Factory->CreateEncoder(
            GUID_ContainerFormatPng,
            nullptr,
            Encoder.GetAddressOf()
        );

    if (FAILED(Result))
    {
        return false;
    }

    Result =
        Encoder->Initialize(
            Stream.Get(),
            WICBitmapEncoderNoCache
        );

    if (FAILED(Result))
    {
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> Frame;

    Result =
        Encoder->CreateNewFrame(
            Frame.GetAddressOf(),
            nullptr
        );

    if (FAILED(Result))
    {
        return false;
    }

    Result =
        Frame->Initialize(nullptr);

    if (FAILED(Result))
    {
        return false;
    }

    Result =
        Frame->SetSize(
            Width,
            Height
        );

    if (FAILED(Result))
    {
        return false;
    }

    WICPixelFormatGUID Format =
        GUID_WICPixelFormat32bppBGRA;

    Result =
        Frame->SetPixelFormat(&Format);

    if (FAILED(Result))
    {
        return false;
    }

    const UINT RowPitch =
        Width * 4;

    const UINT BufferSize =
        RowPitch * Height;

    Result =
        Frame->WritePixels(
            Height,
            RowPitch,
            BufferSize,
            const_cast<BYTE*>(
                reinterpret_cast<const BYTE*>(
                    Pixels.GetData()
                    )
                )
        );

    if (FAILED(Result))
    {
        return false;
    }

    Result =
        Frame->Commit();

    if (FAILED(Result))
    {
        return false;
    }

    Result =
        Encoder->Commit();

    if (FAILED(Result))
    {
        return false;
    }

    return true;
}

FMatrix FImpostorBaker::MakeCaptureView(const FVector& CameraLocation, const FVector& Target) const
{
    FVector Forward = Target - CameraLocation;
    if (Forward.Length() <= EPSILON)
    {
        Forward = FVector(1.0f, 0.0f, 0.0f);
    }
    else
    {
        Forward.Normalize();
    }

    FVector Up(0.0f, 0.0f, 1.0f);
    if (std::abs(Forward.Z) > 0.999f)
    {
        Up = FVector(0.0f, 1.0f, 0.0f);
    }

    FVector Right = Up.Cross(Forward);
    Right.Normalize();

    Up = Forward.Cross(Right);
    Up.Normalize();

    const float Tx = -CameraLocation.Dot(Right);
    const float Ty = -CameraLocation.Dot(Up);
    const float Tz = -CameraLocation.Dot(Forward);

    return FMatrix(
        Right.X, Up.X, Forward.X, 0.0f,
        Right.Y, Up.Y, Forward.Y, 0.0f,
        Right.Z, Up.Z, Forward.Z, 0.0f,
        Tx, Ty, Tz, 1.0f
    );
}

FMatrix FImpostorBaker::MakeCaptureProjection(float OrthoHeight, float NearZ, float FarZ) const
{
    const float AspectRatio = static_cast<float>(TileWidth) / static_cast<float>(TileHeight);
    const float VerticalScale = 2.0f / OrthoHeight;
    const float HorizontalScale = VerticalScale / AspectRatio;
    const float DepthScale = 1.0f / (FarZ - NearZ);

    return FMatrix(
        HorizontalScale, 0.0f, 0.0f, 0.0f,
        0.0f, VerticalScale, 0.0f, 0.0f,
        0.0f, 0.0f, DepthScale, 0.0f,
        0.0f, 0.0f, -NearZ * DepthScale, 1.0f
    );
}
