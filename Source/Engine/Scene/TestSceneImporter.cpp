#include "pch.h"
#include "TestSceneImporter.h"
#include "Core/Serialization/JsonReader.h"
#include "nlohmann/json.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <limits>
#include <stdexcept>

//AI GENERATED FILE

namespace
{
    using FJson = nlohmann::json;
    constexpr double RadiansToDegrees = 57.29577951308232;

    // 원본 UUID 키를 중복 해석 없이 정수로 읽는다.
    std::uint32_t ReadID(const std::string& Key)
    {
        std::uint32_t ID = 0;
        const auto [End, Error] = std::from_chars(Key.data(), Key.data() + Key.size(), ID);

        if (Error != std::errc{} || End != Key.data() + Key.size() || std::to_string(ID) != Key)
            throw std::runtime_error("Invalid primitive UUID: " + Key);

        return ID;
    }

    // 유한한 실수로 구성된 원본 벡터를 읽는다.
    std::array<double, 3> ReadVector(const FJson& Object, const char* Key)
    {
        const auto& Value = Object.at(Key);
        if (!Value.is_array() || Value.size() != 3)
            throw std::runtime_error(std::string("Expected float3: ") + Key);

        std::array<double, 3> Result{};
        
        for (std::size_t Index = 0; Index < 3; ++Index)
        {
            if (!Value[Index].is_number()) throw std::runtime_error("Expected numeric vector.");
        
            Result[Index] = Value[Index].get<double>();
            
            if (!std::isfinite(Result[Index]) || std::abs(Result[Index]) > (std::numeric_limits<float>::max)())
                throw std::runtime_error("Vector exceeds finite float range.");
        }
        
        return Result;
    }

    // 원본 카메라의 단일 원소 배열 값을 읽는다.
    double ReadCameraScalar(const FJson& Camera, const char* Key)
    {
        const auto& Value = Camera.at(Key);
        if (!Value.is_array() || Value.size() != 1 || !Value[0].is_number())
            throw std::runtime_error(std::string("Expected scalar array: ") + Key);

        const double Result = Value[0].get<double>();
        if (!std::isfinite(Result) || std::abs(Result) > (std::numeric_limits<float>::max)())
            throw std::runtime_error("Camera value exceeds finite float range.");
        
        return Result;
    }

    // 매핑 또는 씬 상대 경로를 실제 OBJ 경로로 변환한다.
    std::string ResolveMesh(const std::string& Key, const std::filesystem::path& ScenePath,
        const FTestSceneImporter::FMeshPathMap& MeshPaths)
    {
        const auto Found = MeshPaths.find(Key);
        const auto Path = Found != MeshPaths.end() ? Found->second : ScenePath.parent_path() / std::filesystem::u8path(Key);
        if (Key.empty() || !std::filesystem::is_regular_file(Path))
            throw std::runtime_error("Mesh file not found; provide an explicit mapping: " + Key);
        const auto Utf8 = std::filesystem::absolute(Path).lexically_normal().generic_u8string();
        return std::string(Utf8.begin(), Utf8.end());
    }
}

// 외부 테스트 씬을 엔진 JSON으로 변환하며 파일 쓰기와 객체 생성은 수행하지 않는다.
std::unique_ptr<FJsonReader> FTestSceneImporter::Load(const std::filesystem::path& ScenePath, const FMeshPathMap& MeshPaths)
{
    std::ifstream Stream(ScenePath, std::ios::binary);
    if (!Stream) throw std::runtime_error("Cannot open scene file.");
    std::ostringstream Text;
    Text << Stream.rdbuf();
    if (Stream.bad() || Text.bad()) throw std::runtime_error("Failed to read scene file.");
    // 실제 원본 Default.scene도 JSON이다. 확장자가 아닌 문서 구조로 판별한다.
    FJson Source = FJson::parse(Text.str());
    if (!Source.is_object()) throw std::runtime_error("Scene root must be an object.");
    // 기존 엔진 씬은 변환하지 않고 기존 검증기로 전달한다.
    if (!Source.contains("Primitives")) return FJsonReader::FromDocument(std::move(Source));
    if (Source.contains("Actors") || Source.contains("Components"))
        throw std::runtime_error("Mixed native and test scene formats.");
    const auto& Primitives = Source.at("Primitives");
    if (!Primitives.is_object()) throw std::runtime_error("Primitives must be an object.");

    // 원본 컴포넌트 UUID를 유지하고 충돌하지 않는 Actor UUID 영역을 확보한다.
    const auto& Counter = Source.at("NextUUID");
    if (!Counter.is_number_unsigned()) throw std::runtime_error("NextUUID must be unsigned.");
    std::uint64_t NextID = Counter.get<std::uint64_t>();
    for (const auto& [Key, Value] : Primitives.items())
    {
        const std::uint64_t ID = ReadID(Key);
        if (ID >= NextID) throw std::runtime_error("Primitive UUID exceeds NextUUID.");
    }
    const auto MaxID = (std::numeric_limits<std::uint32_t>::max)();
    if (NextID > MaxID || Primitives.size() >= MaxID - NextID)
        throw std::runtime_error("Insufficient UUID space for imported actors.");
    // 원본에 Version이 없어도 엔진 출력 스키마의 버전은 별도로 지정한다.
    FJson Output = {{"Version", 1}, {"Actors", FJson::object()}, {"Components", FJson::object()}};
    std::unordered_map<std::string, std::string> ResolvedMeshes;
    for (const auto& [Key, Primitive] : Primitives.items())
    {
        // 현재 원본 형식의 StaticMeshComp만 지원하고 미지원 타입을 조용히 누락하지 않는다.
        if (Primitive.at("Type") != "StaticMeshComp")
            throw std::runtime_error("Unsupported primitive type at UUID " + Key);
        const auto Location = ReadVector(Primitive, "Location");
        const auto SourceRotation = ReadVector(Primitive, "Rotation");
        const auto Scale = ReadVector(Primitive, "Scale");
        // 카메라와 같은 축·부호 규칙을 가정하여 XYZ 라디안을 메시의 Roll·Pitch·Yaw 도 단위로 변환한다.
        const std::array<double, 3> Rotation
        {
            SourceRotation[0] * RadiansToDegrees,
            -SourceRotation[1] * RadiansToDegrees,
            SourceRotation[2] * RadiansToDegrees
        };
        const auto MeshKey = Primitive.at("ObjStaticMeshAsset").get<std::string>();
        auto Found = ResolvedMeshes.find(MeshKey);
        if (Found == ResolvedMeshes.end())
            Found = ResolvedMeshes.emplace(MeshKey, ResolveMesh(MeshKey, ScenePath, MeshPaths)).first;
        const auto ActorID = NextID++;
        Output["Actors"][std::to_string(ActorID)] = {
            {"Type", "Actor"}, {"Name", "ImportedActor_" + Key},
            {"RootComponentUUID", ReadID(Key)}, {"bVisible", true}};
        Output["Components"][Key] = {
            {"Type", "StaticMeshComponent"}, {"Name", "ImportedMesh_" + Key}, {"OwnerActorUUID", ActorID},
            {"Location", Location}, {"Rotation", Rotation}, {"Scale", Scale},
            {"bVisible", true}, {"bIsVisible", true}, {"MeshKey", Found->second}};
    }
    Output["NextUUID"] = NextID;

    // 기존 Default_Compatible 씬과 동일한 카메라 변환을 적용한다.
    if (Source.contains("PerspectiveCamera"))
    {
        const auto& Camera = Source.at("PerspectiveCamera");
        const auto Location = ReadVector(Camera, "Location");
        const auto Rotation = ReadVector(Camera, "Rotation");
        const double FOV = ReadCameraScalar(Camera, "FOV");
        const double NearZ = ReadCameraScalar(Camera, "NearClip");
        const double FarZ = ReadCameraScalar(Camera, "FarClip");
        if (FOV <= 0.0 || FOV >= 180.0 || NearZ <= 0.0 || FarZ <= NearZ)
            throw std::runtime_error("Invalid test camera projection.");
        Output["PerspectiveCamera"] = {
            {"Location", Location},
            {"Rotation", {-Rotation[1] * RadiansToDegrees, Rotation[2] * RadiansToDegrees, Rotation[0] * RadiansToDegrees}},
            {"FOV", FOV / RadiansToDegrees}, {"NearZ", NearZ}, {"FarZ", FarZ}};
    }
    return FJsonReader::FromDocument(std::move(Output));
}
