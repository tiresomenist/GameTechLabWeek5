#pragma once

#include <d3d11.h>
#include "MeshResource.h"
#include "StaticMeshData.h"
#include "Core/Name/Name.h"
#include "Core/Container/String.h"
#include "Engine/Object/Object.h"
#include "Engine/Renderer/Material.h"


class UStaticMesh : public UObject
{
	UCLASS(UStaticMesh, "StaticMesh", UObject);
public:
	//UStaticMesh(const FObjectCreateInfo& Info) : Super(Info) {}
	void BuildFromMeshData(const FStaticMeshData& MeshData);
	virtual ~UStaticMesh() ;

	virtual void Serialize(FArchive& Archive) override {};

	FName GetMeshKey() const { return MeshKey; }

	FMeshResource* GetMeshResource() const { return MeshResource; }
	void SetMeshResource(FMeshResource* InResource) { MeshResource = InResource;}

	// StaticMesh가 가지고 있는것 -> 해당 StaticMesh의 Default FMaterial
	const TArray<FMaterial*>& GetDefaultMeshMaterials() const { return Materials; }
	const FMaterial* GetMaterial(uint32 SlotIndex) const
	{
		if (SlotIndex >= Materials.Num())
		{
			return nullptr;
		}
		return Materials[SlotIndex];
	}

	void SetMaterial(uint32 SlotIndex, FMaterial* InMaterial)
	{
		if (SlotIndex >= Materials.Num())
		{
			Materials.resize(SlotIndex + 1);
		}
		Materials[SlotIndex] = InMaterial;
	}

	const FVector& GetBoundsMin() const { return MeshResource ? MeshResource->GetBoundsMin() : FVector::Zero; }
	const FVector& GetBoundsMax() const { return MeshResource ? MeshResource->GetBoundsMax() : FVector::Zero; }
	bool HasBounds() const { return MeshResource ? bHasBounds : false; }

	const TArray<FMeshSection>& GetSections() const { return Sections; } // 섹션 배열 반환
	void SetSections(TArray<FMeshSection>& InSections) { Sections = InSections; }
	void AddSection(uint32 InMaterialSlot, uint32 InStartIndex, uint32 InIndexCount)
	{
		if (InStartIndex + InIndexCount > IndexCount)
		{
			return;
		}
		Sections.Add(FMeshSection{ InMaterialSlot , InStartIndex, InIndexCount });
	}

	const FString& GetSourceFilePath() const { return SourceFilePath; }

	// MeshResource에 들어가는 내용
	//ID3D11Buffer* VertexBuffer;
	//ID3D11Buffer* IndexBuffer;
	//uint32 VertexCount;
	//uint32 IndexCount;
	//uint32 Stride;
	//D3D11_PRIMITIVE_TOPOLOGY TOopology = D3D11_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	
private:
	FName MeshKey;	// 메시 에셋 식별자
	FString SourceFilePath; // 원본 OBJ 경로

	FMeshResource* MeshResource = nullptr;

	TArray<FMaterial*> Materials;
	TArray<FMeshSection> Sections;

	TArray<FStaticMeshObjectInfo> Objects;

	FVector BoundsMin{};
	FVector BoundsMax{};
	bool bHasBounds;

	uint32 StartIndex;
	uint32 IndexCount;

};