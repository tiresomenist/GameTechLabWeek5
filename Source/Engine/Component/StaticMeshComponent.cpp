#include "pch.h"
#include "StaticMeshComponent.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Resource/TextureResource.h"
#include "Engine/Resource/ResourceManager.h"
#include <stdexcept>

void UStaticMeshComponent::SetStaticMesh(const FName& InMeshKey)
{
	if (!InMeshKey.IsNone() && InMeshKey == MeshKey) return;

	int32 NewSlotCount = 0;
	if (!InMeshKey.IsNone())
	{
		// 새 메시를 확보하지 못하면 기존 상태를 변경하지 않는다.
		UStaticMesh* Mesh = GResourceManager::GetInstance()->GetOrLoadStaticMesh(InMeshKey);
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
	if (MeshKey.IsNone()) return;
	GResourceManager* RM = GResourceManager::GetInstance();
	UStaticMesh* Mesh = RM->GetStaticMesh(MeshKey);
	if (!Mesh) return;

	FMeshResource* MeshResource = Mesh->GetMeshResource();
	if (!MeshResource) return;

	for (const FMeshSection& Section : Mesh->GetSections())
	{
		FPrimitiveRenderData OutData{};
		OutData.VertexBuffer = MeshResource->GetVertexBuffer();
		OutData.IndexBuffer = MeshResource->GetIndexBuffer();
		OutData.IndexCount = MeshResource->GetIndexCount();
		OutData.Stride = MeshResource->GetStride();

		OutData.IndexStart = Section.FirstIndex;
		OutData.IndexCount = Section.IndexCount;
		
		OutData.WorldMatrix = &GetWorldMatrix();
		OutData.isSelected = bSelected;
		OutData.Min = MeshResource->GetBoundsMin();
		OutData.Max = MeshResource->GetBoundsMax();


		const FMaterial* SectionMaterial = nullptr;
		if ((Section.MaterialIndex < static_cast<uint32>(OverrideMaterials.Num()) && OverrideMaterials[Section.MaterialIndex]))
		{
			// override 머테리얼 가져오기
			SectionMaterial = OverrideMaterials[Section.MaterialIndex];
		}
		else
		{
			// 기본 머테리얼 가져오기
			SectionMaterial = Mesh->GetMaterial(Section.MaterialIndex);
		}
		if (SectionMaterial)
		{
			OutData.Material = *SectionMaterial;

			OutData.UVTransform.Scale = OutData.Material.UVScale;
			OutData.UVTransform.Offset = OutData.Material.UVOffset;
		}

		ComponentRenderData.Add(OutData);
	}

	return;
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
	const UStaticMesh* Mesh = GetStaticMesh();
	if (!Mesh || !Mesh->HasBounds())
	{
		return false;
	}

	OutMin = Mesh->GetBoundsMin();
	OutMax = Mesh->GetBoundsMax();
	return true;
}

const FMaterial* UStaticMeshComponent::GetMaterial(uint32 MaterialSlot) const
{
	const FMaterial* Mat = Super::GetMaterial(MaterialSlot);
	if (!Mat)
	{
		if (UStaticMesh* Mesh = GetStaticMesh()) 
		{ 
			Mat = Mesh->GetMaterial(MaterialSlot);
		}
	}
	return Mat;
}

const FString& UStaticMeshComponent::GetMaterialPath(uint32 MaterialSlot) const
{
	static const FString EmptyString = "";
	const FMaterial* Mat = GetMaterial(MaterialSlot);
	return Mat ? Mat->TexturePath : EmptyString;
}

UStaticMesh* UStaticMeshComponent::GetStaticMesh() const
{
	if (MeshKey.IsNone())
	{
		return nullptr;
	}
	return GResourceManager::GetInstance()->GetStaticMesh(MeshKey);
}
