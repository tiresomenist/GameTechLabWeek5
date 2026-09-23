#pragma once

#include "Core/Core.h"
#include "Core/Container/String.h"
#include "Core/Name/Name.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Object/ClassType.h"
#include "Engine/Object/ClassRegistry.h"
#include "Engine/Component/ActorComponent.h"
#include "Engine/Resource/MeshNames.h"

#include <charconv>
#include <limits>
#include <stdexcept>

// 씬에 기록된 타입의 복원 방식임
enum class ESceneTypeKind
{
    Unsupported,
    Actor,
    Component,
    LegacyStaticMesh,
    SkipRuntimeWidget
};

// 타입 검증과 실제 복원에서 공유하는 해석 결과임
struct FResolvedSceneType
{
    ESceneTypeKind Kind = ESceneTypeKind::Unsupported;
    FClassType* ClassType = nullptr;

    bool IsValid() const
    {
        if (Kind == ESceneTypeKind::SkipRuntimeWidget)
        {
            return true;
        }

        return
            (Kind == ESceneTypeKind::Actor || Kind == ESceneTypeKind::Component || Kind == ESceneTypeKind::LegacyStaticMesh) && ClassType != nullptr;
    }
};

inline FResolvedSceneType ResolveSceneType(const FName& TypeName)
{
    // 반복 사용하는 타입 이름을 최초 호출 시 한 번 생성함
    static const FName ActorType("Actor");

    static const FName StaticMeshType("StaticMeshComponent");
    static const FName CameraType("CameraComponent");
    static const FName TextType("Text");
    static const FName SpotLightType("SpotLight");
    static const FName WidgetType("WidgetComponent");

    // 현재 등록 이름과 호환용 이름을 함께 유지함
    static const FName FlameType("Flame");
    static const FName FlipbookAlias("FlipbookComponent");

    if (TypeName.IsNone()) { return {}; }

    if (TypeName == ActorType)
    {
        return { ESceneTypeKind::Actor, FClassRegistry::FindClassType(ActorType) };
    }

    // UUID 위젯은 씬 복원 후 다시 생성하므로 직접 복원하지 않음
    if (TypeName == WidgetType)
    {
        return { ESceneTypeKind::SkipRuntimeWidget,nullptr };
    }

    // 대상 클래스의 등록 여부와 컴포넌트 상속 관계를 확인함
    const auto ResolveComponent = [](const FName& ClassName, ESceneTypeKind Kind) -> FResolvedSceneType
        {
            FClassType* ClassType = FClassRegistry::FindClassType(ClassName);

            if (ClassType == nullptr || !ClassType->IsA(UActorComponent::GetClass()))
            { return {}; }

            return { Kind, ClassType };
        };

    const FMeshNames& MeshNames = GetMeshNames();

    // 구형 메시 타입을 현재 StaticMeshComponent로 연결함
    const bool bLegacyStaticMesh =
        TypeName == MeshNames.Plane ||
        TypeName == MeshNames.Cube ||
        TypeName == MeshNames.Sphere ||
        TypeName == MeshNames.Triangle ||
        TypeName == MeshNames.Pepe ||
        TypeName == MeshNames.Octopus ||
        TypeName == MeshNames.Test1 ||
        TypeName == MeshNames.ArrowRed ||
        TypeName == MeshNames.ArrowGreen ||
        TypeName == MeshNames.ArrowBlue;

    if (bLegacyStaticMesh)
    {
        return ResolveComponent(StaticMeshType, ESceneTypeKind::LegacyStaticMesh);
    }

    // 두 표기를 현재 등록된 Flame 클래스로 연결함
    if (TypeName == FlameType || TypeName == FlipbookAlias)
    {
        return ResolveComponent(FlameType, ESceneTypeKind::Component);
    }

    // 현재 씬에서 직접 저장하고 복원하는 타입만 허용함
    const bool bSupportedComponent =
        TypeName == StaticMeshType ||
        TypeName == CameraType ||
        TypeName == TextType ||
        TypeName == SpotLightType;

    if (bSupportedComponent)
    {
        return ResolveComponent(TypeName, ESceneTypeKind::Component);
    }

    return {};
}

inline uint32 ParseSceneUUID(FStringView Text, bool AllowExhaustedCounter = false)
{
    if (Text.empty()) throw std::runtime_error("Empty UUID");
    uint32 Value{};
    const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
    if (Error != std::errc{} || End != Text.data() + Text.size() ||
        (!AllowExhaustedCounter && Value == (std::numeric_limits<uint32>::max)()) ||
        std::to_string(Value) != Text)
        throw std::runtime_error("Invalid UUID");
    return Value;
}

// 씬의 기본 형식을 검사하고 저장된 다음 UUID를 반환한다.
inline uint32 ValidateSceneArchive(FArchive& Archive)
{
    // 검증은 이미 저장된 데이터를 읽는 Archive로 수행한다.
    if (!Archive.IsLoading())
        throw std::runtime_error("Scene validation requires a loading archive.");

    int32 Version = 0;
    uint32 NextUUID = 0;
    Archive.Field("Version", Version);
    Archive.Field("NextUUID", NextUUID);
    if (Version != 1)
        throw std::runtime_error("Unsupported scene version.");

    // 모든 컴포넌트 키가 유효하며 다음 발급 UUID보다 작은지 검사한다.
    uint32 Count = 0;
    if (!Archive.BeginMap("Actors", Count))
        throw std::runtime_error("Missing scene actors.");

    for (uint32 Index = 0; Index < Count; ++Index)
    {
        FString Key;
        Archive.BeginMapEntry(Index, Key);
        if (ParseSceneUUID(Key) >= NextUUID)
            throw std::runtime_error("Actor UUID must be less than NextUUID.");

        // 실제 복원과 같은 타입 해석 규칙으로 지원 여부를 확인한다.
        FString TypeText;
        Archive.Field("Type", TypeText);
		FResolvedSceneType Resolved = ResolveSceneType(FName(TypeText));
        if (Resolved.Kind != ESceneTypeKind::Actor || !Resolved.IsValid())
            throw std::runtime_error("Unsupported scene type: " + TypeText);

        Archive.EndMapEntry();
    }
    Archive.EndMap();

	if (!Archive.BeginMap("Components", Count))
		throw std::runtime_error("Missing scene components.");

    for (uint32 Index = 0; Index < Count; ++Index)
    {
        FString Key;
        Archive.BeginMapEntry(Index, Key);
        if (ParseSceneUUID(Key) >= NextUUID)
            throw std::runtime_error("Component UUID must be less than NextUUID.");

        FString TypeText;
        Archive.Field("Type", TypeText);
		FResolvedSceneType Resolved = ResolveSceneType(FName(TypeText));
        if (Resolved.Kind != ESceneTypeKind::Component || !Resolved.IsValid())
			throw std::runtime_error("Unsupported scene type: " + TypeText);

		Archive.EndMapEntry();
    }
    Archive.EndMap();

    return NextUUID;
}