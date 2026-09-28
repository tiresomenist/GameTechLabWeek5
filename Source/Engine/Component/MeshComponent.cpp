#include <pch.h>
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/MeshResource.h"
#include "Core/Serialization/Archive.h"
#include "Engine\Resource\TextureResource.h"
#include "MeshComponent.h"
#include "Engine/Renderer/Material.h"
#include <utility>

// 컴포넌트 소멸 시 남아 있는 Override 재질을 정리한다.
UMeshComponent::~UMeshComponent()
{
	// 메시 교체에서도 사용할 공통 해제 경로를 재사용한다.
	ClearOverrideMaterials();
}

// 컴포넌트가 소유한 재질 객체를 해제하고 모든 슬롯을 제거한다.
void UMeshComponent::ClearOverrideMaterials()
{
	OverrideMaterials.Empty();
}

void UMeshComponent::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);
	Archive.OptionalField("bIsVisible", bIsVisible);
}

void UMeshComponent::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	for (const auto& OwnedMaterial : OverrideMaterials)
	{
		FMaterial* Mat = OwnedMaterial.get();
		if (Mat &&Mat->bEnableUVScroll)
		{
			if (Mat->ScrollSpeed.X != 0.0f || Mat->ScrollSpeed.Y != 0.0f)
			{
				Mat->UVOffset += Mat ->ScrollSpeed * DeltaTime;
				if (Mat->SamplerName == FName("LinearWrap") || Mat->SamplerName == FName("PointWrap"))
				{
					Mat->UVOffset.X -= std::floor(Mat->UVOffset.X);
					Mat->UVOffset.Y -= std::floor(Mat->UVOffset.Y);
				}
			}
		}
	}
}

void UMeshComponent::SetOverrideMaterial(std::unique_ptr<FMaterial> InMaterial, uint32 MaterialSlot)
{
	if (MaterialSlot >= static_cast<uint32>(OverrideMaterials.Num()))
	{
		OverrideMaterials.SetNum(MaterialSlot + 1);
	}
	OverrideMaterials[MaterialSlot] = std::move(InMaterial);
}

void UMeshComponent::SetOverrideMaterial(const FString& InMaterialPath, uint32 MaterialSlot)
{
	if (InMaterialPath.empty())
	{
		ResetOverrideMaterial(MaterialSlot);
		return;
	}

	GResourceManager* RM = GResourceManager::GetInstance();

	FTextureResource* Texture = RM->GetOrLoadTexture(InMaterialPath);

	if (!Texture || !Texture->GetSRV()) { return; }

	const FMaterial* CurrentMaterial = GetMaterial(MaterialSlot);

	std::unique_ptr<FMaterial> NewMaterial;

	if (CurrentMaterial)
	{
		NewMaterial = std::make_unique<FMaterial>(*CurrentMaterial);

		// 별도 인스턴스로 복제했으므로 새로운 ID.
		NewMaterial->MaterialId = RM->AllocateMaterialId();
	}
	else
	{
		NewMaterial = std::make_unique<FMaterial>(RM->CreateStaticMeshMaterial(Texture->GetSRV(), InMaterialPath));
	}
	NewMaterial->SRV = Texture->GetSRV();
	NewMaterial->TexturePath = InMaterialPath;

	SetOverrideMaterial(std::move(NewMaterial), MaterialSlot);
}

const FString& UMeshComponent::GetMaterialPath(uint32 MaterialSlot) const
{
	static const FString EmptyString = "";
	const FMaterial* Mat = GetMaterial(MaterialSlot);
	if (Mat)
	{
		return Mat->TexturePath;
	}
	return EmptyString;
}
const FMaterial* UMeshComponent::GetMaterial(uint32 MaterialSlot) const
{
	if (MaterialSlot < static_cast<uint32>(OverrideMaterials.Num()))
	{
		return OverrideMaterials[MaterialSlot].get();
	}
	else
		return nullptr;
}

void UMeshComponent::CreateRenderData(TArray<FPrimitiveRenderData>& ComponentRenderData, bool bSelected)
{
	return;
}

FMaterial* UMeshComponent::GetOrCreateOverrideMaterial(uint32 Slot)
{
	if (Slot >= static_cast<uint32>(OverrideMaterials.Num()))
	{
		OverrideMaterials.SetNum(Slot + 1);
	}

	if (OverrideMaterials[Slot])
	{
		return OverrideMaterials[Slot].get();
	}
	GResourceManager* RM = GResourceManager::GetInstance();

	// UStaticMeshComponent에서는 기본 머티리얼 조회까지 수행한다.
	const FMaterial* BaseMaterial = GetMaterial(Slot);

	std::unique_ptr<FMaterial> NewMaterial;

	if (BaseMaterial)
	{
		NewMaterial = std::make_unique<FMaterial>(*BaseMaterial);
		NewMaterial->MaterialId = RM->AllocateMaterialId();
	}
	else
	{
		const FString WhitePath = "Assets/Textures/WhiteTexture.png";
		FTextureResource* White = RM->GetOrLoadTexture(WhitePath);

		if (!White || !White->GetSRV())
		{
			return nullptr;
		}

		NewMaterial = std::make_unique<FMaterial>(
			RM->CreateStaticMeshMaterial(White->GetSRV(), WhitePath));
	}

	OverrideMaterials[Slot] = std::move(NewMaterial);
	return OverrideMaterials[Slot].get();
}

void UMeshComponent::ResetOverrideMaterial(uint32 Slot)
{
	if (Slot < static_cast<uint32>(OverrideMaterials.Num()))
	{
		OverrideMaterials[Slot].reset();
	}
}

bool UMeshComponent::HasOverrideMaterial(uint32 SlotIdx)
{
	if (SlotIdx >= static_cast<uint32>(OverrideMaterials.Num()))
		return false;
	return OverrideMaterials[SlotIdx] != nullptr;
}
