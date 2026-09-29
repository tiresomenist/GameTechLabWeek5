#include "pch.h"
#include "Scene.h"
#include "SceneValidation.h"
#include <memory>
#include <stdexcept>
// TEMP(UI test): Gizmo implementation is currently excluded from the build.
// #include "Engine/Gizmo/UGizmo.h"

#include "Engine/Component/CameraComponent.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Component/ActorComponent.h"
#include "Engine/Component/SceneComponent.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Engine/Component/Primitive/FlipbookComponent.h"
#include "Engine/Component/WidgetComponent.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"

#include "Core/Container/Map.h"

#include "Core/Serialization/Archive.h"
#include "Engine/Object/ClassRegistry.h"
#include "Engine/Log.h"

namespace
{
	constexpr float StaticUniformGridCellSize = 4.0f;

	uint64 MakeStaticUniformGridKey(int32 X, int32 Y, int32 Z)
	{
		constexpr uint64 Mask = (1ull << 21) - 1;
		return ((static_cast<uint64>(X) & Mask) << 42) |
			((static_cast<uint64>(Y) & Mask) << 21) |
			(static_cast<uint64>(Z) & Mask);
	}

	FBoundingBox MakeStaticUniformGridBounds(int32 X, int32 Y, int32 Z)
	{
		const FVector Min(X * StaticUniformGridCellSize, Y * StaticUniformGridCellSize,
			Z * StaticUniformGridCellSize);
		return FBoundingBox(Min, Min + FVector(StaticUniformGridCellSize, StaticUniformGridCellSize,
			StaticUniformGridCellSize));
	}

	struct FPendingActorInfo
	{
		AActor* Actor;
		std::optional<uint32> ParentActorUUID;
		std::optional<uint32> RootComponentUUID;
	};

	struct FPendingComponentInfo
	{
		USceneComponent* Component;
		std::optional<uint32> AttachParentUUID;
	};
}

FSceneType* UScene::GetStaticSceneType()
{
    static FSceneType Type
    {
        .Name = "Scene",
        .SceneConstructor = []() -> UScene* { return new UScene(); },
    };

    return &Type;
}

// UScene의 BeginPlay, Tick, EndPlay는 모든 Scene에 대한 공통 로직이 필요하면 작성
// But 아직 그런 용도가 없음 언젠가 생기면 쓰는걸로...
void UScene::BeginPlay()
{
    for (AActor* Actor : Actors)
    {
        Actor->BeginPlay();
    }
}

void UScene::Tick(float DeltaTime)
{
    for (AActor* Actor : Actors)
    {
        Actor->Tick(DeltaTime);
    }
}

void UScene::EndPlay()
{
    for (AActor* Actor : Actors)
    {
        Actor->EndPlay();
    }
}

void UScene::SetMainCameraSaveData(UCameraComponent* InCamera)
{
    MainCameraSaveData = {
		.Location = InCamera->GetRelativeLocation(),
		.Rotation = InCamera->GetRelativeRotator(),
		.FOV = InCamera->GetFOV(),
		.NearZ = InCamera->GetNearZ(),
		.FarZ = InCamera->GetFarZ(),
    };
}

void UScene::CreateMainCamera()
{
	AActor* CameraActor = SpawnActor<AActor*>(AActor::GetClass());
	MainCamera = static_cast<UCameraComponent*>(CameraActor->CreateComponent(UCameraComponent::GetClass()));
}

void UScene::Serialize(FArchive& Archive)
{
    const bool bLoading = Archive.IsLoading();

    // 메인 카메라 처리. 일단은 값이 없어도 기본값으로 설정
    FCameraSaveData PerspectiveCamera = MainCameraSaveData;
    if (Archive.BeginObject("PerspectiveCamera"))
    {
        TArray<float> Location = { PerspectiveCamera.Location.X, PerspectiveCamera.Location.Y, PerspectiveCamera.Location.Z };
        Archive.Float3OrDefault("Location", Location, 0.0f);

        TArray<float> Rotation = { PerspectiveCamera.Rotation.Pitch, PerspectiveCamera.Rotation.Yaw, PerspectiveCamera.Rotation.Roll };
        Archive.Float3OrDefault("Rotation", Rotation, 0.0f);

		Archive.Field("FOV", PerspectiveCamera.FOV);
		Archive.Field("NearZ", PerspectiveCamera.NearZ);
		Archive.Field("FarZ", PerspectiveCamera.FarZ);

        Archive.EndObject();

        if (bLoading)
        {
            if (!UCameraComponent::AreParametersValid(
                PerspectiveCamera.FOV, 1.0f, PerspectiveCamera.NearZ,
                PerspectiveCamera.FarZ, 0.0f, 10.0f))
            {
                throw std::runtime_error("Invalid saved perspective camera parameters.");
            }

            // 투영 값 검사가 끝난 뒤 저장된 위치·회전과 함께 반영한다.
            PerspectiveCamera.Location = FVector(Location[0], Location[1], Location[2]);
            PerspectiveCamera.Rotation = FRotator(Rotation[0], Rotation[1], Rotation[2]);
            MainCameraSaveData = PerspectiveCamera;
        }
    }

    TArray<FPendingActorInfo> PendingActorInfos;
    TArray<FPendingComponentInfo> PendingComponentInfos;

    TMap<uint32, AActor*> ActorsByUUID;
    TMap<uint32, UActorComponent*> ComponentsByUUID;

    // 액터 처리
    TArray<AActor*> SavedActors;
    if (!bLoading)
    {
        for (AActor* Actor : Actors)
        {
            SavedActors.Add(Actor);
        }
    }
    uint32 ActorCount = static_cast<uint32>(SavedActors.Num());
    if (!Archive.BeginMap("Actors", ActorCount))
        throw std::runtime_error("Missing scene actors.");

    if (bLoading) PendingActorInfos.Reserve(ActorCount);
    for (uint32 Index = 0; Index < ActorCount; ++Index)
    {
        AActor* Actor = bLoading ? nullptr : SavedActors[Index];
        FString Key = bLoading ? FString{} : std::to_string(Actor->GetUUID());
        Archive.BeginMapEntry(Index, Key);

        FString TypeText = bLoading ? FString{} : Actor->GetInstanceClass()->Name.ToString();
        Archive.Field("Type", TypeText);

        if (!bLoading)
        {
            AActor* ParentActor = Actor->GetParentActor();
            if (ParentActor != nullptr)
            {
                uint32 ParentActorUUID = ParentActor->GetUUID();
                Archive.Field("ParentActorUUID", ParentActorUUID);
            }
            USceneComponent* RootComponent = Actor->GetRootComponent();
            if (RootComponent != nullptr)
            {
                uint32 RootComponentUUID = RootComponent->GetUUID();
                Archive.Field("RootComponentUUID", RootComponentUUID);
            }
            Actor->Serialize(Archive);
        }
        else
        {
            // 저장된 타입을 실제 생성할 액터 클래스로 해석한다.
            const FResolvedSceneType Resolved = ResolveSceneType(TypeText);
            if (Resolved.Kind != ESceneTypeKind::Actor || !Resolved.IsValid())
                throw std::runtime_error("Unsupported scene type: " + TypeText);

            const uint32 UUID = ParseSceneUUID(Key);
            uint32 ParentActorUUID = 0;
            uint32 RootComponentUUID = 0;
            const bool bHasParentActorUUID = Archive.OptionalField("ParentActorUUID", ParentActorUUID);
            const bool bHasRootComponentUUID = Archive.OptionalField("RootComponentUUID", RootComponentUUID);

            AActor* Actor = SpawnActor<AActor*>(Resolved.ClassType, UUID);
            Actor->Serialize(Archive);
            ActorsByUUID.Add(UUID, Actor);

            PendingActorInfos.Add({ Actor,
                bHasParentActorUUID ? std::optional(ParentActorUUID) : std::nullopt,
                bHasRootComponentUUID ? std::optional(RootComponentUUID) : std::nullopt });
        }

        Archive.EndMapEntry();
    }

    Archive.EndMap();

    // 컴포넌트 처리
    TArray<UActorComponent*> SavedComponents;
	if (!bLoading)
	{
		for (AActor* Actor : Actors)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				// 지금기준 UUID용 UUID는 저장하지 않음.
				if (!Component->IsA(UWidgetComponent::GetClass()))
					SavedComponents.Add(Component);
			}
		}
	}

	uint32 ComponentCount = static_cast<uint32>(SavedComponents.Num());
	if (!Archive.BeginMap("Components", ComponentCount))
		throw std::runtime_error("Missing scene components.");

	if (bLoading) PendingComponentInfos.Reserve(ComponentCount);
    for (uint32 Index = 0; Index < ComponentCount; ++Index)
    {
		UActorComponent* Component = bLoading ? nullptr : SavedComponents[Index];
		FString Key = bLoading ? FString{} : std::to_string(Component->GetUUID());
        Archive.BeginMapEntry(Index, Key);

		FString TypeText = bLoading ? FString{} : Component->GetInstanceClass()->Name.ToString();
        Archive.Field("Type", TypeText);

        if (!bLoading)
        {
			AActor* Owner = Component->GetOwner();
            if (Owner == nullptr)
				throw std::runtime_error("Saved component has no owner actor.");

			uint32 OwnerActorUUID = Owner->GetUUID();
			Archive.Field("OwnerActorUUID", OwnerActorUUID);

            if (Component->IsA(USceneComponent::GetClass()))
            {
				USceneComponent* SceneComponent = static_cast<USceneComponent*>(Component);
				USceneComponent* AttachParent = SceneComponent->GetAttachParent();
				if (AttachParent != nullptr)
				{
					uint32 AttachParentUUID = AttachParent->GetUUID();
					Archive.Field("AttachParentUUID", AttachParentUUID);
				}
            }

            Component->Serialize(Archive);
        }
        else
        {
			const FResolvedSceneType Resolved = ResolveSceneType(TypeText);
			if (Resolved.Kind != ESceneTypeKind::Component || !Resolved.IsValid())
				throw std::runtime_error("Unsupported scene type: " + TypeText);

			const uint32 UUID = ParseSceneUUID(Key);

            uint32 OwnerActorUUID;
			Archive.Field("OwnerActorUUID", OwnerActorUUID);
            
			AActor** OwnerPtr = ActorsByUUID.Find(OwnerActorUUID);
			if (OwnerPtr == nullptr || *OwnerPtr == nullptr)
				throw std::runtime_error("Invalid owner actor UUID: " + std::to_string(OwnerActorUUID));

            uint32 AttachParentUUID = 0;
			const bool bHasAttachParentUUID = Archive.OptionalField("AttachParentUUID", AttachParentUUID);
			Component = (*OwnerPtr)->CreateComponent(Resolved.ClassType, UUID);
			if (Component == nullptr)
				throw std::runtime_error("Failed to create scene component: " + TypeText);

            Component->Serialize(Archive);
            ComponentsByUUID.Add(UUID, Component);

            if (Component->IsA(USceneComponent::GetClass()))
            {
                PendingComponentInfos.Add({
                    .Component = static_cast<USceneComponent*>(Component),
                    .AttachParentUUID = bHasAttachParentUUID ? std::optional(AttachParentUUID) : std::nullopt
	            });
            }
        }

        Archive.EndMapEntry();
    }

    Archive.EndMap();

    if (!bLoading) return;
    	
    // 모든 액터와 컴포넌트가 생성된 뒤 연결하는 과정
    // 액터 계층 구조 연결
    for (const auto& Pending : PendingActorInfos)
    {
	    if (!Pending.ParentActorUUID)
	    {
            continue;
	    }

		AActor** ParentActorPtr = ActorsByUUID.Find(*Pending.ParentActorUUID);
        if (ParentActorPtr == nullptr || *ParentActorPtr == nullptr || !Pending.Actor->SetParentActor(*ParentActorPtr))
			throw std::runtime_error("Failed to set parent actor for UUID: " + std::to_string(Pending.Actor->GetUUID()));
    }

    // 모든 임시 부모 연결을 먼저 제거하여 생성 순서의 영향을 없앤다.
    for (const FPendingComponentInfo& Pending : PendingComponentInfos)
    {
        Pending.Component->DetachFromParent();
    }

    // 모든 연결이 해제된 상태에서 저장된 부모 관계만 다시 구성한다.
    for (const FPendingComponentInfo& Pending : PendingComponentInfos)
    {
        if (!Pending.AttachParentUUID) continue;

        UActorComponent** ParentPtr = ComponentsByUUID.Find(*Pending.AttachParentUUID);
        if (ParentPtr == nullptr || *ParentPtr == nullptr
            || !(*ParentPtr)->IsA(USceneComponent::GetClass()))
        {
            throw std::runtime_error("Invalid attach parent UUID: "
                + std::to_string(*Pending.AttachParentUUID));
        }

        // 소유 Actor 일치 여부와 순환 관계 검사는 기존 AttachTo에 맡긴다.
        USceneComponent* Parent = static_cast<USceneComponent*>(*ParentPtr);
        if (!Pending.Component->AttachTo(Parent))
        {
            throw std::runtime_error("Failed to attach component UUID: "
                + std::to_string(Pending.Component->GetUUID()));
        }
    }

    // 저장된 루트가 해당 Actor의 루트로 사용 가능한지 확인한 뒤 지정한다.
    for (const FPendingActorInfo& Pending : PendingActorInfos)
    {
        if (!Pending.RootComponentUUID) continue;

        UActorComponent** RootPtr = ComponentsByUUID.Find(*Pending.RootComponentUUID);
        if (RootPtr == nullptr || *RootPtr == nullptr
            || !(*RootPtr)->IsA(USceneComponent::GetClass()))
        {
            throw std::runtime_error("Invalid root component UUID: "
                + std::to_string(*Pending.RootComponentUUID));
        }

        // 다른 Actor 소유이거나 부모가 있는 컴포넌트를 루트로 지정하지 않는다.
        USceneComponent* Root = static_cast<USceneComponent*>(*RootPtr);
        if (Root->GetOwner() != Pending.Actor || !Root->CanBeRootComponent()
            || Root->GetAttachParent() != nullptr)
        {
            throw std::runtime_error("Invalid root component relationship for actor UUID: "
                + std::to_string(Pending.Actor->GetUUID()));
        }

        Pending.Actor->SetRootComponent(Root);
    }

    EnsureUUIDWidgets();
}


void UScene::EnsureUUIDWidgets()
{
	for (AActor* Actor : Actors)
	{
		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root || !Root->IsA(UPrimitiveComponent::GetClass())) continue;

		bool bHasWidget = false;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component->IsA(UWidgetComponent::GetClass()))
			{
				bHasWidget = true;
				break;
			}
		}

		if (!bHasWidget)
		{
			Actor->CreateComponent(UWidgetComponent::GetClass());
		}
	}
}

void UScene::Destroy(UObject* Object)
{
    if (Object == nullptr)
    {
        return;
    }

    if (Object->IsA(AActor::GetClass()))
    {
        DestroyActor(static_cast<AActor*>(Object));
        return;
    }

    if (Object->IsA(UActorComponent::GetClass()))
    {
        UActorComponent* Component = static_cast<UActorComponent*>(Object);
        DestroyActor(Component->GetOwner());
    }
}

void UScene::DestroyActor(AActor* Actor)
{
    if (Actor == nullptr)
    {
        return;
    }

    bool bExistsInScene = false;
    for (AActor* ExistingActor : Actors)
    {
        if (ExistingActor == Actor)
        {
            bExistsInScene = true;
            break;
        }
    }
    if (!bExistsInScene)
    {
        return;
    }

    // 복사본을 사용한다. 자식 삭제 과정에서 부모의 ChildActors는 함께 갱신된다.
    const TArray<AActor*> Children = Actor->GetChildActors();
    for (AActor* Child : Children)
    {
        DestroyActor(Child);
    }

    for (int32 Index = 0; Index < Actors.Num(); ++Index)
    {
        if (Actors[Index] == Actor)
        {
            Actor->EndPlay();
            delete Actor;
            Actors.RemoveAt(Index);
			InvalidateStaticUniformGrid();
            break;
        }
    }
}

void UScene::InvalidateStaticUniformGrid()
{
	bStaticUniformGridDirty = true;
}

const TArray<FStaticUniformGridCell>& UScene::GetStaticUniformGrid() const
{
	BuildStaticUniformGrid();
	return StaticUniformGrid;
}

const TArray<UPrimitiveComponent*>& UScene::GetStaticUniformGridFallbackPrimitives() const
{
	BuildStaticUniformGrid();
	return StaticUniformGridFallbackPrimitives;
}

void UScene::BuildStaticUniformGrid() const
{
	if (!bStaticUniformGridDirty) return;

	StaticUniformGrid.Empty();
	StaticUniformGridFallbackPrimitives.Empty();
	TMap<uint64, int32> CellIndices;
	ForEachPrimitive([&](UPrimitiveComponent* Primitive)
		{
			if (!Primitive->IsA(UStaticMeshComponent::GetClass())) return;

			FVector LocalMin{};
			FVector LocalMax{};
			if (!Primitive->GetLocalBounds(LocalMin, LocalMax))
			{
				StaticUniformGridFallbackPrimitives.Add(Primitive);
				return;
			}

			const FBoundingBox WorldBounds = FBoundingBox(LocalMin, LocalMax).TransformBounds(Primitive->GetWorldMatrix());
			const FVector Center = (WorldBounds.Min + WorldBounds.Max) * 0.5f;
			const int32 CellX = static_cast<int32>(std::floor(Center.X / StaticUniformGridCellSize));
			const int32 CellY = static_cast<int32>(std::floor(Center.Y / StaticUniformGridCellSize));
			const int32 CellZ = static_cast<int32>(std::floor(Center.Z / StaticUniformGridCellSize));
			const int32 MinCellX = static_cast<int32>(std::floor(WorldBounds.Min.X / StaticUniformGridCellSize));
			const int32 MinCellY = static_cast<int32>(std::floor(WorldBounds.Min.Y / StaticUniformGridCellSize));
			const int32 MinCellZ = static_cast<int32>(std::floor(WorldBounds.Min.Z / StaticUniformGridCellSize));
			const int32 MaxCellX = static_cast<int32>(std::floor(WorldBounds.Max.X / StaticUniformGridCellSize));
			const int32 MaxCellY = static_cast<int32>(std::floor(WorldBounds.Max.Y / StaticUniformGridCellSize));
			const int32 MaxCellZ = static_cast<int32>(std::floor(WorldBounds.Max.Z / StaticUniformGridCellSize));

            const int32 SpanX = MaxCellX - MinCellX + 1;
            const int32 SpanY = MaxCellY - MinCellY + 1;
            const int32 SpanZ = MaxCellZ - MinCellZ + 1;

            const bool bTooLargeForGrid = SpanX > 2 || SpanY > 2 || SpanZ > 2;
			if (bTooLargeForGrid)
			{
				StaticUniformGridFallbackPrimitives.Add(Primitive);
				return;
			}

			const uint64 Key = MakeStaticUniformGridKey(CellX, CellY, CellZ);
			int32* CellIndex = CellIndices.Find(Key);

			if (CellIndex == nullptr)
			{
				FStaticUniformGridCell NewCell{};
				NewCell.Key = Key;
				NewCell.SpatialBounds = MakeStaticUniformGridBounds(CellX, CellY, CellZ);
				StaticUniformGrid.Add(std::move(NewCell));
				const int32 NewIndex = StaticUniformGrid.Num() - 1;
				CellIndices.Add(Key, NewIndex);
				CellIndex = CellIndices.Find(Key);
			}
            FStaticUniformGridCell& Cell = StaticUniformGrid[*CellIndex];

            if (Cell.Primitives.IsEmpty())
            {
                Cell.ContentBounds = WorldBounds;
            }
            else
            {
                // 이미 들어 있다면 실제 점유 영역을 확장
                Cell.ContentBounds.Min.X = std::min(Cell.ContentBounds.Min.X, WorldBounds.Min.X);
                Cell.ContentBounds.Min.Y = std::min(Cell.ContentBounds.Min.Y, WorldBounds.Min.Y);
                Cell.ContentBounds.Min.Z = std::min(Cell.ContentBounds.Min.Z, WorldBounds.Min.Z);

                Cell.ContentBounds.Max.X = std::max(Cell.ContentBounds.Max.X, WorldBounds.Max.X);
                Cell.ContentBounds.Max.Y = std::max(Cell.ContentBounds.Max.Y, WorldBounds.Max.Y);
                Cell.ContentBounds.Max.Z = std::max(Cell.ContentBounds.Max.Z, WorldBounds.Max.Z);
            }
            Cell.Primitives.Add(Primitive);
		});
	bStaticUniformGridDirty = false;
}

UScene::~UScene()
{
    for (AActor* Actor : Actors)
    {
        delete Actor;
    }

    Actors.Empty();
}
