#include "pch.h"
#include "StaticMeshComponent.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Resource/TextureResource.h"
#include "Engine/Resource/ResourceManager.h"
#include <stdexcept>
#include <cassert>
#include "Engine/Component/CameraComponent.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Scene/Scene.h"
void UStaticMeshComponent::SetStaticMesh(const FName& InMeshKey)
{
	if (InMeshKey == MeshKey && (InMeshKey.IsNone() || CachedMesh)) return;

	UStaticMesh* Mesh = nullptr;
	int32 NewSlotCount = 0;
	if (!InMeshKey.IsNone())
	{
		// 새 메시를 확보하지 못하면 기존 상태를 변경하지 않는다.
		Mesh = GResourceManager::GetInstance()->GetOrLoadStaticMesh(InMeshKey);
		if (!Mesh)
		{
			throw std::runtime_error("Static mesh could not be loaded: " + InMeshKey.ToString());
		}
		NewSlotCount = Mesh->GetDefaultMeshMaterials().Num();
	}

	// 필요한 배열 용량을 먼저 확보하여 할당 실패 시 기존 재질을 보존한다.
	OverrideMaterials.Reserve(NewSlotCount);

	// 이전 재질을 해제하고 새 메시의 슬롯을 nullptr로 초기화한다.
	ClearOverrideMaterials();
	OverrideMaterials.SetNum(NewSlotCount);
	MeshKey = InMeshKey;
	CachedMesh = Mesh;
	OnWorldBoundsChanged();
}

void UStaticMeshComponent::SetStaticMesh(const FString& FilePath)
{
	SetStaticMesh(FName(FilePath));

}

//FMeshResource* UStaticMeshComponent::GetMeshResource() const
//{
//    return GResourceManager::GetInstance()->GetPrimitive(MeshKey);
//}

void UStaticMeshComponent::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);

	FString MeshKeyValue = (Archive.IsLoading() || MeshKey.IsNone())
		? FString{} : MeshKey.ToString();
	Archive.OptionalField("MeshKey", MeshKeyValue);

	if (Archive.IsLoading() && !MeshKeyValue.empty())
	{
		SetStaticMesh(FName(MeshKeyValue));
	}

	if (Archive.IsSaving())
	{
		uint32 OverrideCount = 0;
		for (uint32 i = 0; i < OverrideMaterials.Num(); ++i)
		{
			if (OverrideMaterials[i] != nullptr)
			{
				++OverrideCount;
			}
		}

		if (Archive.BeginMap("OverrideMaterials", OverrideCount))
		{
			uint32 EntryIdx = 0;
			for (uint32 Slot = 0; Slot < OverrideMaterials.Num(); ++Slot)
			{
				if (OverrideMaterials[Slot] != nullptr)
				{
					FString Key = std::to_string(Slot);
					Archive.BeginMapEntry(EntryIdx++, Key);

					OverrideMaterials[Slot]->Serialize(Archive);

					Archive.EndMapEntry();
				}
			}
			Archive.EndMap();
		}
	}
	else if (Archive.IsLoading())
	{
		uint32 OverrideCount = 0;
		if (Archive.BeginMap("OverrideMaterials", OverrideCount))
		{
			for (uint32 EntryIdx = 0; EntryIdx < OverrideCount; ++EntryIdx)
			{
				FString Key;
				Archive.BeginMapEntry(EntryIdx, Key);

				uint32 Slot = static_cast<uint32>(std::stoul(Key.c_str()));

				FMaterial* Mat = GetOrCreateOverrideMaterial(Slot);
				if (Mat)
				{
					Mat->Serialize(Archive);
				}

				Archive.EndMapEntry();
			}
			Archive.EndMap();
		}
	}
}

void UStaticMeshComponent::CreateRenderData(TArray<FPrimitiveRenderData>& ComponentRenderData,
	const UCameraComponent* Camera, bool bSelected)
{
	if (!CachedMesh || !Camera) return;

	const TArray<FStaticMeshLOD>& LODs = CachedMesh->GetLODs();
	const uint32 LODCount = static_cast<uint32>(LODs.Num());
	if (LODCount == 0) return;

	uint32 LODIndex = 0;
	if (LODCount > 1)
	{
		// Bounds getter마다 LOD를 다시 찾지 않고 LOD0 정보를 재사용합니다.
		const FStaticMeshLOD& BaseLOD = LODs[0];
		const FVector LocalCenter = (BaseLOD.BoundsMin + BaseLOD.BoundsMax) * 0.5f;
		const FMatrix World = GetRenderWorldMatrix(Camera);
		const FVector WorldCenter = World.TransformPosition(LocalCenter);
		const FVector ToCamera = WorldCenter - Camera->GetRelativeLocation();

		const float DistanceSquared = ToCamera.LengthSquared();
		if (DistanceSquared >= 100.0f)
			LODIndex = 1;
		if (LODCount > 2 && DistanceSquared >= 900.0f)
			LODIndex = 2;
	}

	// 위 조건에서 실제 존재하는 LOD 인덱스만 선택합니다.
	const FStaticMeshLOD& LOD = LODs[LODIndex];
	if (!LOD.MeshResource) return;

	const FMeshAllocation& Allocation = LOD.MeshResource->GetAllocation();
	if (Allocation.MeshPageId == InvalidRenderId) return;

	for (const FMeshSection& Section : LOD.Sections)
	{
		if (Section.IndexCount == 0) continue;

		// Override 우선순위와 기본 머티리얼 조회 동작을 유지합니다.
		const FMaterial* SectionMaterial = GetMaterial(Section.MaterialIndex);
		if (!SectionMaterial) continue;

		assert(SectionMaterial->MaterialId != InvalidRenderId);
		assert(Section.FirstIndex <= Allocation.IndexCount);
		assert(Section.IndexCount <= Allocation.IndexCount - Section.FirstIndex);

		FPrimitiveRenderData Data{};
		Data.Geometry.MeshPageId = Allocation.MeshPageId;
		Data.Geometry.FirstIndex = Allocation.FirstIndex + Section.FirstIndex;
		Data.Geometry.IndexCount = Section.IndexCount;
		Data.Geometry.BaseVertex = Allocation.BaseVertex;
		Data.Material = SectionMaterial;

		// 기존 Outline 허용 여부와 선택 상태를 전달합니다.
		Data.Flags = Primitive_AllowOutline;
		if (bSelected) Data.Flags |= Primitive_Selected;

		ComponentRenderData.Add(Data);
	}
}

FMeshResource* UStaticMeshComponent::GetMeshResource() const 
{
	UStaticMesh* Mesh = GetStaticMesh();
	if (!Mesh)
		return nullptr;
	return Mesh->GetMeshResource();
}

bool UStaticMeshComponent::GetLocalBounds(FVector& OutMin, FVector& OutMax) const
{
	if (!CachedMesh || !CachedMesh->HasBounds()) { return false; }
	OutMin = CachedMesh->GetBoundsMin();
	OutMax = CachedMesh->GetBoundsMax();
	return true;
}

const FMaterial* UStaticMeshComponent::GetMaterial(uint32 MaterialSlot) const
{
	if (const FMaterial* Override = Super::GetMaterial(MaterialSlot))
		return Override;

	return CachedMesh ? CachedMesh->GetMaterial(MaterialSlot) : nullptr;
}

const FString& UStaticMeshComponent::GetMaterialPath(uint32 MaterialSlot) const
{
	static const FString EmptyString = "";
	const FMaterial* Mat = GetMaterial(MaterialSlot);
	return Mat ? Mat->TexturePath : EmptyString;
}

UStaticMesh* UStaticMeshComponent::GetStaticMesh() const
{
	return CachedMesh;
}

void UStaticMeshComponent::OnWorldBoundsChanged() const
{
	AActor* Owner = GetOwner();
	UScene* Scene = Owner ? Owner->GetScene() : nullptr;
	if (!Scene) return;

	// 기존 BVH API가 비const 포인터를 받지만, 컴포넌트 자체를 수정하지는 않습니다.
	Scene->UpdateBVH(const_cast<UStaticMeshComponent*>(this));
}