#include "pch.h"
#include "StaticMeshComponent.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Resource/TextureResource.h"
#include "Engine/Resource/ResourceManager.h"
#include <stdexcept>
#include <cassert>

void UStaticMeshComponent::SetStaticMesh(const FName& InMeshKey)
{
	if ((!InMeshKey.IsNone() || CachedMesh) && InMeshKey == MeshKey) return;

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

void UStaticMeshComponent::CreateRenderData(TArray<FPrimitiveRenderData>& ComponentRenderData, bool bSelected)
{
	FMeshResource* Resource = CachedMesh->GetMeshResource();
	if (!Resource){	return; }

	const FMeshAllocation& Allocation = Resource->GetAllocation();

	//잘못된 메쉬페이지 아이디
	if (Allocation.MeshPageId == InvalidRenderId){ return; }

	for (const FMeshSection& Section : CachedMesh->GetSections())
	{
		if (Section.IndexCount == 0){ continue; }

		const FMaterial* SectionMaterial = GetMaterial(Section.MaterialIndex);

		if (!SectionMaterial){ continue; }

		assert(SectionMaterial->MaterialId != InvalidRenderId);
		assert(Section.FirstIndex <= Allocation.IndexCount);
		assert(Section.IndexCount <= Allocation.IndexCount - Section.FirstIndex);

		FPrimitiveRenderData Data{};

		Data.Geometry.MeshPageId = Allocation.MeshPageId;
		Data.Geometry.FirstIndex = Allocation.FirstIndex + Section.FirstIndex;
		Data.Geometry.IndexCount = Section.IndexCount;
		Data.Geometry.BaseVertex = Allocation.BaseVertex;

		Data.Material = SectionMaterial;

		Data.Flags = Primitive_AllowOutline;
		if (bSelected){	Data.Flags |= Primitive_Selected; }

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