#include "pch.h"

#include "Core/Util/File.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ResourceManager.h"
#include "Engine/Renderer/PrimitiveRenderData.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Engine/Resource/MeshData/Sphere.h"
#include "Engine/Resource/MeshData/Cube.h"
#include "Engine/Resource/MeshData/Plane.h"
#include "Engine/Resource/MeshData/Flame.h"
#include "Engine/Resource/MeshData/Triangle.h"
#include "Engine/Resource/MeshData/PePe.h"
#include "Engine/Resource/MeshData/Octopus.h"
#include "Engine/Resource/MeshData/ArrowRed.h"
#include "Engine/Resource/MeshData/ArrowGreen.h"
#include "Engine/Resource/MeshData/ArrowBlue.h"
#include "Engine/Resource/MeshData/MoveRed.h"
#include "Engine/Resource/MeshData/MoveGreen.h"
#include "Engine/Resource/MeshData/MoveBlue.h"
#include "Engine/Resource/MeshData/ScaleRed.h"
#include "Engine/Resource/MeshData/ScaleGreen.h"
#include "Engine/Resource/MeshData/ScaleBlue.h"
#include "Engine/Resource/MeshData/RotateRed.h"
#include "Engine/Resource/MeshData/RotateGreen.h"
#include "Engine/Resource/MeshData/RotateBlue.h"
#include "Engine/Resource/MeshData/Grid.h"
#include "Engine/Resource/MeshData/rocket_mesh.h"
#include "Core/Math/Vector.h"
#include "Engine/Resource/TextureResource.h"
#include "GeometryGenerator.h"
#include "Engine/Resource/MeshNames.h"
#include "Engine/Log.h"
#include <new>
#include <memory>
#include <limits>
#include <stdexcept>
#include <cmath>
#include <d3dcompiler.h>
#include <format>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <cwchar>

namespace
{
    // 직접 경로나 기본 모델 이름을 기존 규칙으로 해석하여 절대 OBJ 경로를 반환한다.
    std::filesystem::path ResolveObjPath(const FName& MeshKey)
    {
        if (MeshKey.IsNone()) return {};

        // 직접 지정한 파일이 존재하면 우선 사용한다.
        const FString KeyText = MeshKey.ToString();
        std::filesystem::path ObjPath = File::PathFromUtf8(KeyText);
        std::error_code Error;
        if (!std::filesystem::is_regular_file(ObjPath, Error))
        {
            // 기존 "Cube" 요청은 Assets/Models/Cube.obj로 해석한다.
            ObjPath = std::filesystem::path("Assets/Models")
                / File::PathFromUtf8(KeyText + ".obj");
            Error.clear();
            if (!std::filesystem::is_regular_file(ObjPath, Error)) return {};
        }

        // 상대 경로와 . 및 .. 표현을 정리하여 같은 캐시 키를 사용하게 한다.
        Error.clear();
        std::filesystem::path AbsolutePath = std::filesystem::absolute(ObjPath, Error);
        if (Error) return {};
        return AbsolutePath.lexically_normal();
    }
    // OBJ 경로에 확장자를 덧붙여 같은 폴더의 캐시 경로를 반환한다.
    std::filesystem::path GetMeshCachePath(const std::filesystem::path& ObjPath)
    {
        // Cube.obj를 Cube.obj.meshcache로 만들어 원본 파일명 전체를 유지한다.
        std::filesystem::path CachePath = ObjPath;
        CachePath += ".meshcache";
        return CachePath;
    }

    // 원본 파일 하나의 현재 상태를 조회하고, 성공했을 때만 결과를 전달한다.
    bool TryReadMeshSourceFile(const std::filesystem::path& Path, FStaticMeshSourceFile& OutSource)
    {
        if (Path.empty()) return false;

        // 경로와 파일 상태 조회 실패는 캐시를 사용할 수 없는 것으로 처리한다.
        std::error_code Error;
        const std::filesystem::path AbsolutePath =
            std::filesystem::absolute(Path, Error).lexically_normal();
        if (Error) return false;
        if (!std::filesystem::is_regular_file(AbsolutePath, Error) || Error) return false;

        const auto FileSize = std::filesystem::file_size(AbsolutePath, Error);
        if (Error) return false;
        const auto WriteTime = std::filesystem::last_write_time(AbsolutePath, Error);
        if (Error) return false;

        // 모든 조회가 성공한 뒤 정수값을 문자열로 보존하고 출력에 반영한다.
        FStaticMeshSourceFile Source;
        Source.FilePath = AbsolutePath;
        Source.FileSize = std::to_string(FileSize);
        Source.LastWriteTime = std::to_string(WriteTime.time_since_epoch().count());
        OutSource = std::move(Source);
        return true;
    }

    // Import에서 수집한 모든 원본의 상태를 기록하고, 전부 성공하면 결과를 전달한다.
    bool TryCaptureMeshSourceFiles(const TArray<std::filesystem::path>& Paths,
        TArray<FStaticMeshSourceFile>& OutSources)
    {
        if (Paths.IsEmpty()) return false;

        // 일부 파일만 기록된 목록이 전달되지 않도록 임시 배열에 수집한다.
        TArray<FStaticMeshSourceFile> Sources;
        Sources.Reserve(Paths.Num());
        for (const std::filesystem::path& Path : Paths)
        {
            FStaticMeshSourceFile Source;
            if (!TryReadMeshSourceFile(Path, Source)) return false;
            Sources.Add(std::move(Source));
        }

        // OBJ가 첫 항목인 Import 결과의 순서를 그대로 유지한다.
        OutSources = std::move(Sources);
        return true;
    }

    // 캐시가 요청한 OBJ의 것이며 모든 원본 파일 상태가 그대로인지 검사한다.
    bool AreMeshSourcesCurrent(const FStaticMeshData& MeshData,
        const std::filesystem::path& ObjPath)
    {
        if (ObjPath.empty() || MeshData.SourceFiles.IsEmpty()) return false;

        // 다른 OBJ의 캐시를 잘못 재사용하지 않도록 대표 원본 경로를 비교한다.
        std::error_code Error;
        const std::filesystem::path AbsoluteObjPath =
            std::filesystem::absolute(ObjPath, Error).lexically_normal();
        if (Error || MeshData.SourceFiles[0].FilePath != AbsoluteObjPath) return false;

        // 파일이 사라졌거나 크기·수정 시각이 하나라도 달라지면 재생성이 필요하다.
        for (const FStaticMeshSourceFile& SavedSource : MeshData.SourceFiles)
        {
            if (!SavedSource.FilePath.is_absolute()) return false;

            FStaticMeshSourceFile CurrentSource;
            if (!TryReadMeshSourceFile(SavedSource.FilePath, CurrentSource)) return false;
            if (SavedSource.FileSize != CurrentSource.FileSize ||
                SavedSource.LastWriteTime != CurrentSource.LastWriteTime)
            {
                return false;
            }
        }
        return true;
    }
    // 유효한 바이너리를 복원하며, 사용할 수 없으면 OBJ를 읽지 않고 false를 반환한다.
    bool TryLoadMeshCache(const std::filesystem::path& ObjPath, FStaticMeshData& OutMeshData)
    {
        const std::filesystem::path CachePath = GetMeshCachePath(ObjPath);
        const FString ObjPathText = File::PathToUtf8(ObjPath);
        std::error_code Error;
        if (!std::filesystem::is_regular_file(CachePath, Error)) return false;

        try
        {
            // 기존 바이너리 형식 검사와 원본 변경 검사를 그대로 재사용한다.
            FStaticMeshData CachedData = FStaticMeshData::LoadBinary(CachePath);
            if (CachedData.Vertices.IsEmpty() || !AreMeshSourcesCurrent(CachedData, ObjPath))
            {
                UE_LOG("[MeshCache] Invalidated: {}", ObjPathText);
                return false;
            }

            // 검증을 모두 통과한 데이터만 호출자에게 전달한다.
            OutMeshData = std::move(CachedData);
            UE_LOG("[MeshCache] Hit: {}", ObjPathText);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            // 메모리 부족을 캐시 손상으로 간주하여 더 큰 Import 작업으로 이어가지 않는다.
            throw;
        }
        catch (const std::exception& ErrorInfo)
        {
            // 손상되거나 지원하지 않는 바이너리는 호출자가 대체 경로를 결정한다.
            UE_LOG("[MeshCache] Read failed: {} ({})", ObjPathText, ErrorInfo.what());
            return false;
        }
    }
    void CheckRenderResourceHR(HRESULT Result, const char* Operation)
    {
        if (FAILED(Result))
        {
            throw std::runtime_error(std::format("{} failed. HRESULT: {}", Operation, Result));
        }
    }

    Microsoft::WRL::ComPtr<ID3DBlob> CompileResourceShader(const WCHAR* FilePath,const char* EntryPoint,
        const char* ShaderModel)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> ShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> ErrorBlob;

        const HRESULT Result = D3DCompileFromFile(FilePath,nullptr,nullptr, EntryPoint, ShaderModel,
            0,0,ShaderBlob.GetAddressOf(),ErrorBlob.GetAddressOf());

        if (ErrorBlob)
        {
            UE_LOG("Shader diagnostic: {}",static_cast<const char*>(ErrorBlob->GetBufferPointer()));
        }

        CheckRenderResourceHR(Result, "D3DCompileFromFile");
        return ShaderBlob;
    }
}
// 완성된 CPU 메시 데이터를 GPU 리소스로 구성하고 절대 경로 키로 등록한다.
UStaticMesh* GResourceManager::BuildAndCacheStaticMesh(
    const std::filesystem::path& ObjPath, FStaticMeshData& MeshData)
{
    // 호출자는 ResolveObjPath 또는 프리로드 검색에서 확정한 절대 경로를 전달한다.
    const FString ObjPathText = File::PathToUtf8(ObjPath);
    const FName CacheKey(ObjPathText);
    if (UStaticMesh** Existing = StaticMeshCache.Find(CacheKey))
        return *Existing;

    // CPU 데이터와 GPU 리소스가 같은 원본 경로를 식별자로 사용하게 한다.
    MeshData.PathFileName = ObjPathText;
    std::unique_ptr<UStaticMesh> NewMesh(
        static_cast<UStaticMesh*>(FObjectFactory::ConstructEngineObject(UStaticMesh::GetClass())));
    NewMesh->BuildFromMeshData(MeshData);
    if (!NewMesh->GetMeshResource())
        throw std::runtime_error("Failed to create static mesh GPU resource: " + ObjPathText);

    // 생성에 성공한 객체만 등록하고, 중복 등록이면 기존 객체를 반환한다.
    if (!StaticMeshCache.Add(CacheKey, NewMesh.get()))
        return *StaticMeshCache.Find(CacheKey);

    // 성공한 객체는 기존 전역 UObject 관리 체계의 수명에 따라 유지한다.
    return NewMesh.release();
}
// 지정한 폴더의 바이너리 캐시를 검색하고 유효한 메시만 GPU 리소스까지 준비한다.
FStaticMeshPreloadResult GResourceManager::PreloadCachedStaticMeshes(
    const std::filesystem::path& Root,
    const std::function<void(const FStaticMeshPreloadResult&)>& OnProgress,
    const std::function<bool()>& ShouldCancel)
{
    if (Root.empty())
        throw std::invalid_argument("Mesh preload root is empty");
    if (!Device || !Device->GetDevice())
        throw std::logic_error("Mesh preload requires an initialized resource manager");

    FStaticMeshPreloadResult Result;
    TArray<std::filesystem::path> PendingDirectories;
    TArray<std::filesystem::path> CacheFiles;

    // 현재 스냅샷을 호출자에게 전달하며 콜백 오류는 상위 호출자로 전파한다.
    const auto PublishProgress = [&]()
        {
            if (OnProgress) OnProgress(Result);
        };

    // 안전한 작업 경계에서 취소를 확인하고 최종 결과에도 기록한다.
    const auto CheckCancellation = [&]() -> bool
        {
            if (ShouldCancel && ShouldCancel())
                Result.bCancelled = true;
            return Result.bCancelled;
        };

    // 검색 오류는 파일 처리 개수와 분리하여 기록한다.
    const auto ReportSearchError = [&](const std::filesystem::path& Path,
        const std::error_code& Error)
        {
            ++Result.SearchErrorCount;
            UE_LOG("[MeshPreload] Search failed: {} ({})",
                File::PathToUtf8(Path), Error.message());
        };

    PublishProgress();
    if (CheckCancellation())
    {
        PublishProgress();
        return Result;
    }

    // 검색 시작점부터 절대 경로를 사용하여 일반 로더와 캐시 키를 통일한다.
    std::error_code Error;
    const std::filesystem::path AbsoluteRoot =
        std::filesystem::absolute(Root, Error).lexically_normal();
    if (Error)
    {
        ReportSearchError(Root, Error);
        Result.bSearchComplete = true;
        PublishProgress();
        return Result;
    }
    PendingDirectories.Add(AbsoluteRoot);

    // 디렉터리를 직접 순회하여 한 폴더의 접근 실패가 다른 폴더 검색을 막지 않게 한다.
    while (!PendingDirectories.IsEmpty())
    {
        if (CheckCancellation()) break;
        const std::filesystem::path Directory = PendingDirectories.Pop();
        Result.CurrentFile = Directory;
        PublishProgress();

        // 루트 경로를 포함해 디렉터리 심볼릭 링크에는 진입하지 않는다.
        Error.clear();
        const auto DirectoryStatus = std::filesystem::symlink_status(Directory, Error);
        if (Error)
        {
            ReportSearchError(Directory, Error);
            continue;
        }
        if (std::filesystem::is_symlink(DirectoryStatus)) continue;
        if (!std::filesystem::is_directory(DirectoryStatus))
        {
            ReportSearchError(Directory,
                std::make_error_code(std::errc::not_a_directory));
            continue;
        }

        std::filesystem::directory_iterator It(
            Directory, std::filesystem::directory_options::none, Error);
        const std::filesystem::directory_iterator End;
        if (Error)
        {
            ReportSearchError(Directory, Error);
            continue;
        }

        while (It != End)
        {
            if (CheckCancellation()) break;
            const std::filesystem::path EntryPath = It->path();
            std::error_code EntryError;
            const auto EntryStatus = It->symlink_status(EntryError);

            // 실제 디렉터리는 검색 목록에, 실제 캐시 파일은 처리 목록에 넣는다.
            if (EntryError)
            {
                ReportSearchError(EntryPath, EntryError);
            }
            else if (std::filesystem::is_directory(EntryStatus))
            {
                PendingDirectories.Add(EntryPath);
            }
            else if (std::filesystem::is_regular_file(EntryStatus)
                && _wcsicmp(EntryPath.extension().c_str(), L".meshcache") == 0)
            {
                CacheFiles.Add(EntryPath);
                ++Result.TotalCount;
            }

            Error.clear();
            It.increment(Error);
            if (Error)
            {
                ReportSearchError(Directory, Error);
                break;
            }
        }
    }

    if (CheckCancellation())
    {
        PublishProgress();
        return Result;
    }

    // 검색 완료 후 파일 경로순으로 처리하여 실행마다 순서를 일정하게 유지한다.
    CacheFiles.Sort();
    Result.bSearchComplete = true;
    Result.CurrentFile.clear();
    PublishProgress();

    for (const std::filesystem::path& CachePath : CacheFiles)
    {
        if (CheckCancellation()) break;
        Result.CurrentFile = CachePath;
        PublishProgress();

        // 진행 알림을 전달하는 사이 들어온 취소도 파일 작업 전에 반영한다.
        if (CheckCancellation()) break;

        try
        {
            // Cube.obj.meshcache에서 마지막 확장자만 제거하여 원본 경로를 얻는다.
            std::filesystem::path ObjPath = CachePath;
            ObjPath.replace_extension();
            const FName CacheKey(File::PathToUtf8(ObjPath));

            if (_wcsicmp(ObjPath.extension().c_str(), L".obj") != 0)
            {
                ++Result.SkippedCount;
                UE_LOG("[MeshPreload] Skipped unexpected name: {}",
                    File::PathToUtf8(CachePath));
            }
            else if (StaticMeshCache.Find(CacheKey))
            {
                // 이미 준비된 메시에는 파일 읽기나 GPU 생성을 반복하지 않는다.
                ++Result.SkippedCount;
            }
            else
            {
                FStaticMeshData MeshData;
                if (!TryLoadMeshCache(ObjPath, MeshData))
                {
                    // 무효한 캐시는 제외하며 OBJ Import·Cook·재저장을 호출하지 않는다.
                    ++Result.SkippedCount;
                    UE_LOG("[MeshPreload] Skipped unusable cache: {}",
                        File::PathToUtf8(CachePath));
                }
                else
                {
                    // 일반 로더와 동일한 생성·등록 경로로 완성된 메시를 캐시에 넣는다.
                    BuildAndCacheStaticMesh(ObjPath, MeshData);
                    ++Result.LoadedCount;
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            // 메모리 부족은 다음 파일로 진행하지 않고 시작 실패로 전달한다.
            throw;
        }
        catch (const std::exception& ErrorInfo)
        {
            // 장치 자체가 손실되었다면 개별 파일 오류로 처리하지 않는다.
            if (FAILED(Device->GetDevice()->GetDeviceRemovedReason()))
                throw;

            // 개별 파일의 리소스 생성 실패는 기록하고 다음 파일을 처리한다.
            ++Result.FailedCount;
            UE_LOG("[MeshPreload] Failed: {} ({})",
                File::PathToUtf8(CachePath), ErrorInfo.what());
        }

        PublishProgress();
    }

    // 마지막 파일 처리 중 들어온 취소까지 최종 결과에 반영한다.
    CheckCancellation();
    Result.CurrentFile.clear();
    PublishProgress();
    return Result;
}
GResourceManager* GResourceManager::GetInstance()
{
	static GResourceManager Instance;
	return &Instance;
}

void GResourceManager::Initialize(GDevice* InDevice)
{
    Device = InDevice;
	if (!Device || !Device->GetDevice() || !DefaultFont.Build(Device->GetDevice(), "Assets/Fonts/Pretendard-Regular.ttf", 24.0f))
		throw std::runtime_error("Default font atlas build failed");
    RegisterDefaultPrimitives(InDevice);
    RegisterTexturePrimitives(InDevice);
    RegisterDefaultRenderResources();
    RegisterDefaultRasterizerStates();
    RegisterDefaultBlendStates();
    RegisterDefaultDepthStencilStates();
}

FMeshResource* GResourceManager::CreateMesh(const FName& MeshName,
    std::span<const FVertexSimple> Vertices, std::span<const uint32> Indices)
{
    if (MeshName.IsNone()) { return nullptr; }
    if (FMeshResource** Existing = PrimitiveCache.Find(MeshName)) {
        return *Existing;
    }
    if (Vertices.empty() || Indices.empty()) return nullptr;
    const size_t MaxBytes = (std::numeric_limits<UINT>::max)();
    if (Vertices.size() > MaxBytes / sizeof(FVertexSimple) || Indices.size() > MaxBytes / sizeof(uint32))
        return nullptr;
    for (const auto& V : Vertices)
        if (!std::isfinite(V.x) || !std::isfinite(V.y) || !std::isfinite(V.z)) return nullptr;
    if (!Device || !Device->GetDevice()) return nullptr;
    for (uint32 Index : Indices)
        if (Index >= Vertices.size()) return nullptr;

    auto Mesh = std::make_unique<FMeshResource>();
    // CPU 피킹과 바운딩 계산에 필요한 로컬 위치만 보관함
    for (const auto& Vertex : Vertices)
        Mesh->Positions.Add(FVector(Vertex.x, Vertex.y, Vertex.z));
    Mesh->indexes.GetVector().assign(Indices.begin(), Indices.end());
    Mesh->VertexCount = static_cast<UINT>(Vertices.size());
    Mesh->IndexCount = static_cast<UINT>(Indices.size());
    Mesh->Stride = sizeof(FVertexSimple);
    // GPU에는 색상 정보를 포함한 원본 정점을 업로드함
    Mesh->VertexBuffer = Device->CreateVertexBuffer(Vertices.data(), Mesh->Stride * Mesh->VertexCount);
    if (!Mesh->VertexBuffer) return nullptr;
    Mesh->IndexBuffer = Device->CreateIndexBuffer(&Mesh->indexes[0], sizeof(uint32) * Mesh->IndexCount);
    if (!Mesh->IndexBuffer) return nullptr;
    Mesh->bHasBounds = false;
    if (Mesh->Positions.Num() > 0)
    {
        Mesh->BoundsMin = Mesh->Positions[0];
        Mesh->BoundsMax = Mesh->BoundsMin;

        for (const FVector& Position : Mesh->Positions)
        {
            Mesh->BoundsMin.X = (std::min)(Mesh->BoundsMin.X, Position.X);
            Mesh->BoundsMin.Y = (std::min)(Mesh->BoundsMin.Y, Position.Y);
            Mesh->BoundsMin.Z = (std::min)(Mesh->BoundsMin.Z, Position.Z);

            Mesh->BoundsMax.X = (std::max)(Mesh->BoundsMax.X, Position.X);
            Mesh->BoundsMax.Y = (std::max)(Mesh->BoundsMax.Y, Position.Y);
            Mesh->BoundsMax.Z = (std::max)(Mesh->BoundsMax.Z, Position.Z);
        }

        Mesh->bHasBounds = true;
    }

    if (PrimitiveCache.Add(MeshName, Mesh.get())){
        return Mesh.release();
    }
    // 등록되지 않은 임시 Mesh는 unique_ptr이 해제함
    return GetPrimitive(MeshName);
    
}

FMeshResource* GResourceManager::CreateTexturedMesh(const FName& MeshName,
    std::span<const FVertexTexture> Vertices, std::span<const uint32> Indices)
{
    // 같은 이름의 텍스처 메시를 재사용하며 다른 정점 형식과의 충돌을 거부함
    if (MeshName.IsNone()) { return nullptr; }
    if (FMeshResource** Existing = PrimitiveCache.Find(MeshName)) {
        return (*Existing)->GetStride() == sizeof(FVertexTexture) ? *Existing : nullptr;
    }
    if (!Device || !Device->GetDevice()) return nullptr;
    if (Vertices.empty() || Indices.empty() || Indices.size() % 3 != 0) return nullptr;

    // 버퍼 크기를 UINT로 변환하기 전에 곱셈 오버플로를 검사함
    const size_t VertexCount = Vertices.size();
    const size_t IndexCount = Indices.size();
    const size_t MaxBytes = (std::numeric_limits<UINT>::max)();
    if (VertexCount > MaxBytes / sizeof(FVertexTexture) || IndexCount > MaxBytes / sizeof(uint32))
        return nullptr;

    for (const auto& Vertex : Vertices)
    {
        if (!std::isfinite(Vertex.x) || !std::isfinite(Vertex.y) || !std::isfinite(Vertex.z) ||
            !std::isfinite(Vertex.u) || !std::isfinite(Vertex.v))
            return nullptr;
    }
    for (uint32 Index : Indices)
        if (Index >= VertexCount) return nullptr;

    // 생성 도중 실패하면 이미 생성된 버퍼도 메시 소멸자에서 해제함
    auto Mesh = std::make_unique<FMeshResource>();
    Mesh->VertexCount = static_cast<UINT>(VertexCount);
    Mesh->IndexCount = static_cast<UINT>(IndexCount);
    Mesh->Stride = sizeof(FVertexTexture);
    // 입력은 소유하지 않는 뷰이므로 CPU 피킹용 인덱스는 자체 배열에 복사함
    Mesh->indexes.SetNum(IndexCount);
    std::copy(Indices.begin(), Indices.end(), Mesh->indexes.begin());

    // CPU에는 피킹과 바운딩 계산에 사용하는 위치만 보관함
    Mesh->Positions.SetNum(VertexCount);
    for (size_t Index = 0; Index < VertexCount; ++Index)
    {
        const auto& Vertex = Vertices[Index];
        Mesh->Positions[Index] = FVector(Vertex.x, Vertex.y, Vertex.z);
    }

    // GPU에는 위치와 UV가 포함된 원본 정점을 업로드함
    Mesh->VertexBuffer = Device->CreateVertexBuffer(
        Vertices.data(), static_cast<UINT>(VertexCount * sizeof(FVertexTexture)));
    if (!Mesh->VertexBuffer) return nullptr;

    Mesh->IndexBuffer = Device->CreateIndexBuffer(
        &Mesh->indexes[0], static_cast<UINT>(IndexCount * sizeof(uint32)));
    if (!Mesh->IndexBuffer) return nullptr;

    // 로컬 위치의 축별 최솟값과 최댓값으로 바운딩 박스를 계산함
    Mesh->BoundsMin = Mesh->Positions[0];
    Mesh->BoundsMax = Mesh->Positions[0];
    for (const FVector& Position : Mesh->Positions)
    {
        Mesh->BoundsMin.X = (std::min)(Mesh->BoundsMin.X, Position.X);
        Mesh->BoundsMin.Y = (std::min)(Mesh->BoundsMin.Y, Position.Y);
        Mesh->BoundsMin.Z = (std::min)(Mesh->BoundsMin.Z, Position.Z);
        Mesh->BoundsMax.X = (std::max)(Mesh->BoundsMax.X, Position.X);
        Mesh->BoundsMax.Y = (std::max)(Mesh->BoundsMax.Y, Position.Y);
        Mesh->BoundsMax.Z = (std::max)(Mesh->BoundsMax.Z, Position.Z);
    }
    Mesh->bHasBounds = true;

    if (PrimitiveCache.Add(MeshName, Mesh.get()))
    {
        return Mesh.release();
    }

    // 등록되지 않은 임시 Mesh는 unique_ptr이 해제함
    return GetPrimitive(MeshName);
}

FMeshResource* GResourceManager::CreateStaticMeshResource(const FName& MeshName,
    std::span<const FVertexPNCT> Vertices, std::span<const uint32> Indices)
{
    if (MeshName.IsNone()) { return nullptr; }
    // TODO:: Stride이용 메쉬 구분-> 추후Stride가 같은 메쉬 추가시 문제 발생 수정 필요 
    if (FMeshResource** Existing = PrimitiveCache.Find(MeshName)) {
        return (*Existing)->GetStride() == sizeof(FVertexPNCT) ? *Existing : nullptr;
    }
    if (!Device || !Device->GetDevice()) return nullptr;
    if (Vertices.empty() || Indices.empty() || Indices.size() % 3 != 0) return nullptr;

    // 버퍼 크기를 UINT로 변환하기 전에 곱셈 오버플로를 검사함
    const size_t VertexCount = Vertices.size();
    const size_t IndexCount = Indices.size();
    const size_t MaxBytes = (std::numeric_limits<UINT>::max)();
    if (VertexCount > MaxBytes / sizeof(FVertexPNCT) || IndexCount > MaxBytes / sizeof(uint32))
        return nullptr;

    for (const auto& Vertex : Vertices)
    {
        if (!std::isfinite(Vertex.x) || !std::isfinite(Vertex.y) || !std::isfinite(Vertex.z) ||
            !std::isfinite(Vertex.u) || !std::isfinite(Vertex.v))
            return nullptr;
    }
    for (uint32 Index : Indices)
        if (Index >= VertexCount) return nullptr;

    // 생성 도중 실패하면 이미 생성된 버퍼도 메시 소멸자에서 해제함
    auto Mesh = std::make_unique<FMeshResource>();
    Mesh->VertexCount = static_cast<UINT>(VertexCount);
    Mesh->IndexCount = static_cast<UINT>(IndexCount);
    Mesh->Stride = sizeof(FVertexPNCT);
    // 입력은 소유하지 않는 뷰이므로 CPU 피킹용 인덱스는 자체 배열에 복사함
    Mesh->indexes.SetNum(IndexCount);
    std::copy(Indices.begin(), Indices.end(), Mesh->indexes.begin());

    // CPU에는 피킹과 바운딩 계산에 사용하는 위치만 보관함
    Mesh->Positions.SetNum(VertexCount);
    for (size_t Index = 0; Index < VertexCount; ++Index)
    {
        const auto& Vertex = Vertices[Index];
        Mesh->Positions[Index] = FVector(Vertex.x, Vertex.y, Vertex.z);
    }

    // GPU에는 위치와 UV가 포함된 원본 정점을 업로드함
    Mesh->VertexBuffer = Device->CreateVertexBuffer(
        Vertices.data(), static_cast<UINT>(VertexCount * sizeof(FVertexPNCT)));
    if (!Mesh->VertexBuffer) return nullptr;

    Mesh->IndexBuffer = Device->CreateIndexBuffer(
        &Mesh->indexes[0], static_cast<UINT>(IndexCount * sizeof(uint32)));
    if (!Mesh->IndexBuffer) return nullptr;

    // 로컬 위치의 축별 최솟값과 최댓값으로 바운딩 박스를 계산함
    Mesh->BoundsMin = Mesh->Positions[0];
    Mesh->BoundsMax = Mesh->Positions[0];
    for (const FVector& Position : Mesh->Positions)
    {
        Mesh->BoundsMin.X = (std::min)(Mesh->BoundsMin.X, Position.X);
        Mesh->BoundsMin.Y = (std::min)(Mesh->BoundsMin.Y, Position.Y);
        Mesh->BoundsMin.Z = (std::min)(Mesh->BoundsMin.Z, Position.Z);
        Mesh->BoundsMax.X = (std::max)(Mesh->BoundsMax.X, Position.X);
        Mesh->BoundsMax.Y = (std::max)(Mesh->BoundsMax.Y, Position.Y);
        Mesh->BoundsMax.Z = (std::max)(Mesh->BoundsMax.Z, Position.Z);
    }
    Mesh->bHasBounds = true;

    if (PrimitiveCache.Add(MeshName, Mesh.get()))
    {
        return Mesh.release();
    }

    // 등록되지 않은 임시 Mesh는 unique_ptr이 해제함
    return GetPrimitive(MeshName);
}

void GResourceManager::Shutdown()
{
    for (auto& [type, mesh] : PrimitiveCache)
    {
        delete mesh;
    }
    for (auto& [Path, Texture] : TextureCache)
    {
        delete Texture;
    }
    // UStaticMesh는 GObjectStatics에서 해제하므로 여기서는 조회 정보만 비운다.
    StaticMeshAliases.Empty();
    StaticMeshCache.Empty();
    TextureCache.Empty();
    PrimitiveCache.Empty();
    DefaultFont.Release();
    TextureMaterialConstantBuffer.Reset();
    WireframePixelShader.Reset();
    ShaderCache.Empty();
    SamplerCache.Empty();
    RasterizerStateCache.Empty();
    BlendStateCache.Empty();
    DepthStencilStateCache.Empty();

    Device = nullptr;

}

FMeshResource* GResourceManager::GetPrimitive(const FName& MeshName)
{
    if (MeshName.IsNone()){return nullptr;}

    if (FMeshResource** Found = PrimitiveCache.Find(MeshName))
    {
        return *Found;
    }

    return nullptr;
}

FTextureResource* GResourceManager::GetOrLoadTexture(const FString& FilePath)
{
    // 이미 로드한 경로라면 기존 리소스를 반환함
    if (FTextureResource** Existing = TextureCache.Find(FilePath))
    {
        return *Existing;
    }

    if (!Device || !Device->GetDevice())
    {
        throw std::runtime_error("Texture device is not initialized");
    }

    // 로딩이나 캐시 등록이 실패하면 임시 객체를 자동 해제함
    auto NewTexture = std::make_unique<FTextureResource>();

    NewTexture->Load(Device->GetDevice(), FilePath);

    // 로딩에 성공한 리소스만 캐시에 등록함
    if (!TextureCache.Add(FilePath, NewTexture.get()))
    {
        // 같은 키가 이미 존재하면 임시 객체를 해제하고 기존 값을 반환함
        return *TextureCache.Find(FilePath);
    }

    // 캐시 등록 후 소유권을 리소스 매니저로 이전함
    return NewTexture.release();
}

// MeshKey - 메시파일 경로 또는 메시 이름
// 메모리 캐시·바이너리·OBJ 순서로 메시를 확보하고 GPU 리소스를 생성한다.
// 메모리 캐시를 우선 재사용하고, 없으면 바이너리 또는 OBJ에서 메시를 구성한다.
UStaticMesh* GResourceManager::GetOrLoadStaticMesh(const FName& MeshKey)
{
    if (MeshKey.IsNone()) return nullptr;

    // 이미 성공한 요청은 경로 변환과 파일 상태 조회 없이 메모리에서 찾는다.
    const FName* KnownKey = StaticMeshAliases.Find(MeshKey);
    if (UStaticMesh** Existing = StaticMeshCache.Find(KnownKey ? *KnownKey : MeshKey))
        return *Existing;

    // 처음 보는 요청만 실제 파일 경로로 해석한다.
    const std::filesystem::path ObjPath = ResolveObjPath(MeshKey);
    if (ObjPath.empty()) return nullptr;
    const FString ObjPathText = File::PathToUtf8(ObjPath);
    const FName CacheKey(ObjPathText);

    UStaticMesh* Mesh = nullptr;
    if (UStaticMesh** Existing = StaticMeshCache.Find(CacheKey))
    {
        // 다른 표현의 경로로 이미 로딩한 동일 메시를 재사용한다.
        Mesh = *Existing;
    }
    else
    {
        FStaticMeshData MeshData;
        if (!TryLoadMeshCache(ObjPath, MeshData))
        {
            // 일반 로딩에서만 바이너리를 사용할 수 없을 때 OBJ를 해석한다.
            UE_LOG("[MeshCache] Import: {}", ObjPathText);
            const FObjInfo RawData = FObjImporter::Import(ObjPath);
            MeshData = FObjImporter::Cook(RawData);

            // 기존 원본 상태 기록과 바이너리 저장 정책을 유지한다.
            try
            {
                if (TryCaptureMeshSourceFiles(RawData.SourceFiles, MeshData.SourceFiles))
                {
                    MeshData.SaveBinary(GetMeshCachePath(ObjPath));
                    UE_LOG("[MeshCache] Saved: {}", ObjPathText);
                }
                else
                {
                    UE_LOG("[MeshCache] Save skipped: source state unavailable ({})", ObjPathText);
                }
            }
            catch (const std::bad_alloc&)
            {
                // 메모리 부족은 정상적인 캐시 저장 실패와 구분한다.
                throw;
            }
            catch (const std::exception& ErrorInfo)
            {
                UE_LOG("[MeshCache] Save failed: {} ({})", ObjPathText, ErrorInfo.what());
            }
        }

        // 바이너리와 OBJ 경로 모두 동일한 GPU 생성·등록 함수를 사용한다.
        Mesh = BuildAndCacheStaticMesh(ObjPath, MeshData);
    }

    // 성공한 요청만 기억하며 실제 메시 포인터는 절대 경로 캐시에만 보관한다.
    if (MeshKey != CacheKey)
        StaticMeshAliases.Add(MeshKey, CacheKey);
    return Mesh;
}

void GResourceManager::RegisterShader(const FName& Name, const WCHAR* FilePath,
    const char* VSEntry, const char* PSEntry, const TArray<D3D11_INPUT_ELEMENT_DESC>& Layout)
{
    if (!Device || !Device->GetDevice())
    {
        throw std::runtime_error("Shader device is not initialized");
    }

    if (Name.IsNone() ||!FilePath || !*FilePath ||!VSEntry || !*VSEntry || !PSEntry || !*PSEntry ||Layout.IsEmpty())
    {
        throw std::invalid_argument("Invalid shader registration");
    }

    if (ShaderCache.Contains(Name))
    {
        throw std::logic_error(std::format("Shader already registered: {}", Name.ToString()));
    }

    ID3D11Device* NativeDevice = Device->GetDevice();

    FShaderResource Resource;

    const auto VSBlob = CompileResourceShader(FilePath, VSEntry, "vs_5_0");

    //버텍스 셰이더 생성 시도
    CheckRenderResourceHR(
        NativeDevice->CreateVertexShader(
            VSBlob->GetBufferPointer(),
            VSBlob->GetBufferSize(),
            nullptr,
            Resource.VertexShader.GetAddressOf()),
        "CreateVertexShader");

    // 인풋 레이아웃 생성 시도
    CheckRenderResourceHR(
        NativeDevice->CreateInputLayout(
            Layout.GetData(),
            static_cast<UINT>(Layout.Num()),
            VSBlob->GetBufferPointer(),
            VSBlob->GetBufferSize(),
            Resource.InputLayout.GetAddressOf()),
        "CreateInputLayout");

    const auto PSBlob =
        CompileResourceShader(FilePath, PSEntry, "ps_5_0");

    // 픽셀 셰이더 생성 시도
    CheckRenderResourceHR(
        NativeDevice->CreatePixelShader(
            PSBlob->GetBufferPointer(),
            PSBlob->GetBufferSize(),
            nullptr,
            Resource.PixelShader.GetAddressOf()),
        "CreatePixelShader");

    if (!ShaderCache.Add(Name, Resource))
    {
        throw std::logic_error("Failed to register shader");
    }
}

const FShaderResource* GResourceManager::GetShader(const FName& Name) const
{
    return ShaderCache.Find(Name);
}

ID3D11PixelShader* GResourceManager::GetWireframePixelShader() const
{
    return WireframePixelShader.Get();
}

void GResourceManager::RegisterSampler(const FName& Name, const D3D11_SAMPLER_DESC& Desc)
{
    if (!Device || !Device->GetDevice())
    {
        throw std::runtime_error("Sampler device is not initialized");
    }

    if (Name.IsNone())
    {
        throw std::invalid_argument("Invalid sampler name");
    }

    if (SamplerCache.Contains(Name))
    {
        throw std::logic_error(std::format("Sampler already registered: {}", Name.ToString()));
    }

    Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;

    //샘플러 생성 시도
    CheckRenderResourceHR(
        Device->GetDevice()->CreateSamplerState(
            &Desc,
            Sampler.GetAddressOf()),
        "CreateSamplerState");

    if (!SamplerCache.Add(Name, Sampler))
    {
        throw std::logic_error("Failed to register sampler");
    }
}

ID3D11SamplerState* GResourceManager::GetSampler(const FName& Name) const
{
    const auto* Found = SamplerCache.Find(Name);
    return Found ? Found->Get() : nullptr;
}

FMaterial GResourceManager::CreateColorMaterial() const
{
    static const FName ShaderName("Mesh.Color");

    FMaterial Material{};
    Material.Shader = GetShader(ShaderName);
    return Material;
}

FMaterial GResourceManager::CreateTextureMaterial(ID3D11ShaderResourceView* SRV) const
{
    static const FName ShaderName("Mesh.Texture");
    static const FName SamplerName("LinearClamp");

    FMaterial Material{};
    Material.SRV = SRV;
    Material.Shader = GetShader(ShaderName);
    Material.Sampler = GetSampler(SamplerName);
    Material.ConstantBuffer = TextureMaterialConstantBuffer.Get();
    return Material;
}

FMaterial GResourceManager::CreateStaticMeshMaterial(ID3D11ShaderResourceView* SRV, const FString& InTexturePath,bool bClamp) const
{
    static const FName ShaderName("Mesh.StaticMesh");
    static const FName ClampSamplerName("LinearClamp");
    static const FName WrapSamplerName("LinearWrap");

    FMaterial Material{};
    Material.SRV = SRV;
    Material.Shader = GetShader(ShaderName);
    Material.Sampler = GetSampler(bClamp ? ClampSamplerName : WrapSamplerName);
    Material.ConstantBuffer = TextureMaterialConstantBuffer.Get();
    Material.TexturePath = InTexturePath;
    return Material;
}

void GResourceManager::RegisterDefaultPrimitives(GDevice* InDevice)
{
    const FMeshNames& Names = GetMeshNames();

    //if (!CreateMesh(Names.Triangle, triangle_vertices, triangle_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateTexturedMesh(Names.Flame, flame_vertices, flame_indices)) throw std::runtime_error("Flame mesh creation failed");
    if (!CreateTexturedMesh(Names.Pepe, pepe_vertices, pepe_indices)) throw std::runtime_error("Pepe mesh creation failed");
    if (!CreateMesh(Names.Octopus, octopus_vertices, octopus_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.ArrowRed, arrow_red_vertices, arrow_red_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.ArrowGreen, arrow_green_vertices, arrow_green_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.ArrowBlue, arrow_blue_vertices, arrow_blue_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.MoveRed, move_red_vertices, move_red_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.MoveGreen, move_green_vertices, move_green_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.MoveBlue, move_blue_vertices, move_blue_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.ScaleRed, scale_red_vertices, scale_red_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.ScaleGreen, scale_green_vertices, scale_green_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.ScaleBlue, scale_blue_vertices, scale_blue_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.RotateRed, rotate_red_vertices, rotate_red_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.RotateGreen, rotate_green_vertices, rotate_green_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.RotateBlue, rotate_blue_vertices, rotate_blue_indices)) throw std::runtime_error("Required mesh creation failed");
    if (!CreateMesh(Names.Grid, grid_vertices, grid_indices)) throw std::runtime_error("Required mesh creation failed");
}


void GResourceManager::RegisterTexturePrimitives(GDevice* InDevice)
{
    const FMeshNames& Names = GetMeshNames();

    TArray<FVertexTexture> CubeVertices;
    TArray<uint32> CubeIndices;
    FGeometryGenerator::CreateCube(2.0f, 2.0f, 2.0f, CubeVertices, CubeIndices);

    if (!CreateTexturedMesh(Names.Cube, CubeVertices, CubeIndices))
    {
        throw std::runtime_error("TexturedCube mesh creation failed");
    }

    TArray<FVertexTexture> SphereVertices;
    TArray<uint32> SphereIndices;
    // 반지름, 세로로 자르는 개수, 가로로 자르는 개수
    FGeometryGenerator::CreateSphere(1.0f, 64, 32, SphereVertices, SphereIndices);

    if (!CreateTexturedMesh(Names.Sphere, SphereVertices, SphereIndices))
    {
        throw std::runtime_error("TexturedSphere mesh creation failed");
    }

    TArray<FVertexTexture> PlaneVertices;
    TArray<uint32> PlaneIndices;
    FGeometryGenerator::CreatePlane(1.0f, 1.0f, 1, 1, PlaneVertices, PlaneIndices);

    if (!CreateTexturedMesh(Names.Plane, PlaneVertices, PlaneIndices))
    {
        throw std::runtime_error("TexturedPlane mesh creation failed");
    }

    TArray<FVertexTexture> TriangleVertices;
    TArray<uint32> TriangleIndices;
    FGeometryGenerator::CreateTriangle(1.0f, 1.0f, TriangleVertices, TriangleIndices);

    if (!CreateTexturedMesh(Names.Triangle, TriangleVertices, TriangleIndices))
    {
        throw std::runtime_error("TexturedTriangle mesh creation failed");
    }

    // Blender에서 내보낸 UV 포함 로켓 메시
    if (!CreateTexturedMesh("Rocket", rocket_vertices, rocket_indices))
    {
        throw std::runtime_error("Rocket mesh creation failed");
    }

    // SpotLight의 빌보드 아이콘용 정점데이터
    const TArray<FVertexTexture> IconVertices
    {
        { -0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f }, //좌하단
        { 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },  //우하단
        { 0.5f,  0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },  //우상단
        { -0.5f,  0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f }  //좌상단
    };
    const TArray<uint32> IconIndices
    {
        0, 1, 2,
        0, 2, 3
    };
    // 모든 SpotLight가 재사용할 아이콘 메시 등록함
    if (!CreateTexturedMesh(Names.SpotLightIcon, IconVertices, IconIndices))
    {
        throw std::runtime_error("SpotLight icon mesh creation failed");
    }
}

void GResourceManager::RegisterDefaultRenderResources()
{
    const TArray<D3D11_INPUT_ELEMENT_DESC> ColorLayout
    {
        {
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
        {
            "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
            0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
    };

    RegisterShader(FName("Mesh.Color"),L"Assets/Shaders/MainShader.hlsl","mainVS","mainPS",ColorLayout);

    RegisterShader(FName("Editor.Highlight"), L"Assets/Shaders/MainShader.hlsl",
        "VS_Highlight", "PS_Highlight", ColorLayout);
    RegisterShader(FName("Editor.Grid"), L"Assets/Shaders/GridShader.hlsl",
        "VS_Grid", "PS_Grid", ColorLayout);
    RegisterShader(FName("Editor.BatchLine"), L"Assets/Shaders/BatchLineShader.hlsl",
        "mainVS", "mainPS", ColorLayout);

    // 와이어프레임은 메시의 VS를 유지하고 PS만 교체하므로 별도로 소유한다.
    const auto WireframeBlob = CompileResourceShader(
        L"Assets/Shaders/WireframeShader.hlsl", "mainPS", "ps_5_0");
    CheckRenderResourceHR(
        Device->GetDevice()->CreatePixelShader(
            WireframeBlob->GetBufferPointer(), WireframeBlob->GetBufferSize(),
            nullptr, WireframePixelShader.GetAddressOf()),
        "CreateWireframePixelShader");

    const TArray<D3D11_INPUT_ELEMENT_DESC> TextureLayout
    {
        {
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
        {
            "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
            0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
        {
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
            0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
    };

    const TArray<D3D11_INPUT_ELEMENT_DESC> StaticMeshLayout
    {
        {
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
        {
            "NORMAL" , 0, DXGI_FORMAT_R32G32B32_FLOAT,
            0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
        {
            "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
            0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
        {
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
            0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0
        },
    };

    RegisterShader(FName("Mesh.Texture"),L"Assets/Shaders/TextureShader.hlsl","mainVS","mainPS",TextureLayout);
    RegisterShader(FName("Editor.Text"), L"Assets/Shaders/TextShader.hlsl","mainVS_Text","mainPS_Text",TextureLayout);
    RegisterShader(FName("Mesh.StaticMesh"), L"Assets/Shaders/StaticMeshShader.hlsl", "mainVS", "mainPS", StaticMeshLayout);

    D3D11_SAMPLER_DESC SamplerDesc{};
    SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    SamplerDesc.MinLOD = 0.0f;
    SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    RegisterSampler(FName("LinearClamp"), SamplerDesc);

    D3D11_SAMPLER_DESC WrapSamplerDesc{};
    WrapSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    WrapSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    WrapSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    WrapSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    WrapSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    WrapSamplerDesc.MinLOD = 0.0f;
    WrapSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    RegisterSampler(FName("LinearWrap"), WrapSamplerDesc);

    //  Linear Mirror (거울 대칭 반복)
    D3D11_SAMPLER_DESC MirrorDesc = WrapSamplerDesc;
    MirrorDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
    MirrorDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
    MirrorDesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
    RegisterSampler(FName("LinearMirror"), MirrorDesc);

    D3D11_SAMPLER_DESC FontSamplerDesc{};
    FontSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    FontSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    FontSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    FontSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    FontSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    FontSamplerDesc.MinLOD = 0.0f;
    FontSamplerDesc.MaxLOD = 0.0f;

    RegisterSampler(FName("Font.LinearClamp"), FontSamplerDesc);

    // Point Clamp (도트/픽셀 외곽 고정)
    D3D11_SAMPLER_DESC PointClampDesc{};
    PointClampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    PointClampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    PointClampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    PointClampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    PointClampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    RegisterSampler(FName("PointClamp"), PointClampDesc);

    // Point Wrap (도트/픽셀 반복)
    D3D11_SAMPLER_DESC PointWrapDesc = PointClampDesc;
    PointWrapDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    PointWrapDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    PointWrapDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    RegisterSampler(FName("PointWrap"), PointWrapDesc);

    // Point Mirror
    D3D11_SAMPLER_DESC PointMirrorDesc = PointClampDesc;
    PointMirrorDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
    PointMirrorDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
    PointMirrorDesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
    RegisterSampler(FName("PointMirror"), PointMirrorDesc);

    D3D11_BUFFER_DESC Desc{};
    Desc.ByteWidth = sizeof(FTextureDrawConstants);
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    CheckRenderResourceHR(
        Device->GetDevice()->CreateBuffer(
            &Desc, nullptr, TextureMaterialConstantBuffer.GetAddressOf()),
        "CreateTextureMaterialConstantBuffer");
}

void GResourceManager::RegisterRasterizerState(const FName& Name, const D3D11_RASTERIZER_DESC& Desc)
{
    if (!Device || !Device->GetDevice())
    {
        throw std::runtime_error("Rasterizer device is not initialized");
    }

    if (Name.IsNone())
    {
        throw std::invalid_argument("Invalid rasterizer state name");
    }

    if (RasterizerStateCache.Contains(Name))
    {
        throw std::logic_error(std::format("Rasterizer state already registered: {}", Name.ToString()));
    }

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> State;

    CheckRenderResourceHR(
        Device->GetDevice()->CreateRasterizerState(
            &Desc, State.GetAddressOf()),
        "CreateRasterizerState");

    if (!RasterizerStateCache.Add(Name, State))
    {
        throw std::logic_error("Failed to register rasterizer state");
    }
}

ID3D11RasterizerState* GResourceManager::GetRasterizerState(const FName& Name) const
{
    const auto* Found = RasterizerStateCache.Find(Name);
    return Found ? Found->Get() : nullptr;
}

void GResourceManager::RegisterDefaultRasterizerStates()
{
    // 일반 메시: 뒷면 컬링
    D3D11_RASTERIZER_DESC SolidDesc{};
    SolidDesc.FillMode = D3D11_FILL_SOLID;
    SolidDesc.CullMode = D3D11_CULL_BACK;
    SolidDesc.ScissorEnable = TRUE;

    RegisterRasterizerState(FName("Rasterizer.SolidBack"), SolidDesc);

    // 양면 렌더링
    D3D11_RASTERIZER_DESC CullNoneDesc = SolidDesc;
    CullNoneDesc.CullMode = D3D11_CULL_NONE;

    RegisterRasterizerState(FName("Rasterizer.SolidNone"), CullNoneDesc);

    // 하이라이트: 앞면 컬링
    D3D11_RASTERIZER_DESC CullFrontDesc = SolidDesc;
    CullFrontDesc.CullMode = D3D11_CULL_FRONT;

    RegisterRasterizerState(FName("Rasterizer.SolidFront"),CullFrontDesc);

    // 와이어프레임: 일반 메시와 같은 컬링
    D3D11_RASTERIZER_DESC WireDesc = SolidDesc;
    WireDesc.FillMode = D3D11_FILL_WIREFRAME;

    RegisterRasterizerState(FName("Rasterizer.WireBack"), WireDesc);
}

void GResourceManager::RegisterBlendState(const FName& Name, const D3D11_BLEND_DESC& Desc)
{
    if (!Device || !Device->GetDevice())
    {
        throw std::runtime_error("Blend state device is not initialized");
    }

    if (Name.IsNone())
    {
        throw std::invalid_argument("Invalid blend state name");
    }

    if (BlendStateCache.Contains(Name))
    {
        throw std::logic_error(std::format("Blend state already registered: {}", Name.ToString()));
    }

    Microsoft::WRL::ComPtr<ID3D11BlendState> State;

    CheckRenderResourceHR(Device->GetDevice()->CreateBlendState(&Desc, State.GetAddressOf()), "CreateBlendState");

    if (!BlendStateCache.Add(Name, State))
    {
        throw std::logic_error("Failed to register blend state");
    }
}

ID3D11BlendState* GResourceManager::GetBlendState(const FName& Name) const
{
    const auto* Found = BlendStateCache.Find(Name);
    return Found ? Found->Get() : nullptr;
}
void GResourceManager::RegisterDefaultBlendStates()
{
    // 텍스트·그리드·배치 라인에서 사용하는 알파 블렌딩
    D3D11_BLEND_DESC AlphaDesc{};
    AlphaDesc.AlphaToCoverageEnable = FALSE;
    AlphaDesc.IndependentBlendEnable = FALSE;

    auto& AlphaTarget = AlphaDesc.RenderTarget[0];
    AlphaTarget.BlendEnable = TRUE;
    AlphaTarget.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    AlphaTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    AlphaTarget.BlendOp = D3D11_BLEND_OP_ADD;

    AlphaTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
    AlphaTarget.DestBlendAlpha = D3D11_BLEND_ZERO;
    AlphaTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    AlphaTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    RegisterBlendState(FName("Blend.Alpha"), AlphaDesc);

    // 불꽃 RGB에 알파를 곱하여 기존 화면 RGB에 더함
    D3D11_BLEND_DESC AdditiveDesc{};
    auto& AdditiveTarget = AdditiveDesc.RenderTarget[0];

    AdditiveTarget.BlendEnable = TRUE;
    AdditiveTarget.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    AdditiveTarget.DestBlend = D3D11_BLEND_ONE;
    AdditiveTarget.BlendOp = D3D11_BLEND_OP_ADD;

    // 기존 화면 알파 유지
    AdditiveTarget.SrcBlendAlpha = D3D11_BLEND_ZERO;
    AdditiveTarget.DestBlendAlpha = D3D11_BLEND_ONE;
    AdditiveTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    AdditiveTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    RegisterBlendState(FName("Blend.Additive"), AdditiveDesc);
}

void GResourceManager::RegisterDepthStencilState(const FName& Name, const D3D11_DEPTH_STENCIL_DESC& Desc)
{
    if (!Device || !Device->GetDevice())
    {
        throw std::runtime_error("Depth stencil device is not initialized");
    }

    if (Name.IsNone())
    {
        throw std::invalid_argument("Invalid depth stencil state name");
    }

    if (DepthStencilStateCache.Contains(Name))
    {
        throw std::logic_error(std::format("Depth stencil state already registered: {}", Name.ToString()));
    }

    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> State;

    CheckRenderResourceHR(
        Device->GetDevice()->CreateDepthStencilState(
            &Desc, State.GetAddressOf()),
        "CreateDepthStencilState");

    if (!DepthStencilStateCache.Add(Name, State))
    {
        throw std::logic_error("Failed to register depth stencil state");
    }
}

ID3D11DepthStencilState* GResourceManager::GetDepthStencilState(const FName& Name) const
{
    const auto* Found = DepthStencilStateCache.Find(Name);
    return Found ? Found->Get() : nullptr;
}

void GResourceManager::RegisterDefaultDepthStencilStates()
{
    // 일반 메시: 깊이 검사·기록
    D3D11_DEPTH_STENCIL_DESC DefaultDesc{};
    DefaultDesc.DepthEnable = TRUE;
    DefaultDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    DefaultDesc.DepthFunc = D3D11_COMPARISON_LESS;
    DefaultDesc.StencilEnable = FALSE;

    RegisterDepthStencilState(FName("Depth.Default"), DefaultDesc);

    // 기즈모: 깊이 검사 비활성화
    D3D11_DEPTH_STENCIL_DESC GizmoDesc = DefaultDesc;
    GizmoDesc.DepthEnable = FALSE;

    RegisterDepthStencilState(FName("Depth.Gizmo"), GizmoDesc);

    // 깊이는 검사하지만 기록하지 않는 상태
    D3D11_DEPTH_STENCIL_DESC ReadOnlyDesc{};
    ReadOnlyDesc.DepthEnable = TRUE;
    ReadOnlyDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ReadOnlyDesc.DepthFunc = D3D11_COMPARISON_LESS;

    RegisterDepthStencilState(FName("Depth.Highlight"), ReadOnlyDesc);
    RegisterDepthStencilState(FName("Depth.Translucent"), ReadOnlyDesc);
    RegisterDepthStencilState(FName("Depth.Text"), ReadOnlyDesc);

    // 선택된 메시: 스텐실 마스크 기록
    D3D11_DEPTH_STENCIL_DESC StencilWriteDesc = DefaultDesc;
    StencilWriteDesc.StencilEnable = TRUE;
    StencilWriteDesc.StencilReadMask = 0xFF;
    StencilWriteDesc.StencilWriteMask = 0xFF;

    StencilWriteDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    StencilWriteDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    StencilWriteDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
    StencilWriteDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;

    StencilWriteDesc.BackFace = StencilWriteDesc.FrontFace;

    RegisterDepthStencilState(FName("Depth.StencilWrite"), StencilWriteDesc);

    // 외곽선: 깊이 검사를 끄고 스텐실 마스크 바깥만 표시
    D3D11_DEPTH_STENCIL_DESC OutlineDesc{};
    OutlineDesc.DepthEnable = FALSE;
    OutlineDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    OutlineDesc.DepthFunc = D3D11_COMPARISON_LESS;
    OutlineDesc.StencilEnable = TRUE;
    OutlineDesc.StencilReadMask = 0xFF;
    OutlineDesc.StencilWriteMask = 0x00;

    OutlineDesc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
    OutlineDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    OutlineDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    OutlineDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;

    OutlineDesc.BackFace = OutlineDesc.FrontFace;

    RegisterDepthStencilState(FName("Depth.Outline"), OutlineDesc);
}