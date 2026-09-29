#pragma once

#include <d3d11.h>
#include "MeshResource.h"
#include "StaticMeshData.h"
#include "Core/Name/Name.h"
#include "Core/Container/String.h"
#include "Engine/Object/Object.h"
#include "Engine/Renderer/Material.h"
#include <memory>
#include <utility>

struct FImpostorLOD {
	FString TexturePath;

	uint32 ViewCountX = 8;
	uint32 ViewCountY = 4;

	FVector Pivot = FVector::Zero;

	float Width = 1.0f;
	float Height = 1.0f;

	uint32 MaterialIndex = InvalidRenderId;

	bool IsValid() const {
		return !TexturePath.empty() && MaterialIndex != InvalidRenderId;
	}
};

struct FStaticMeshLOD
{
	FMeshResource* MeshResource = nullptr;
	TArray<FMeshSection> Sections;

	FVector BoundsMin{};
	FVector BoundsMax{};
	bool bHasBounds = false;

	float ScreenSize = 0.0f;

	bool bImpostor = false;
	FImpostorLOD Impostor;
};

class UStaticMesh : public UObject
{
	UCLASS(UStaticMesh, "StaticMesh", UObject);
public:
	//UStaticMesh(const FObjectCreateInfo& Info) : Super(Info) {}
	void BuildFromMeshData(const FStaticMeshData& MeshData);
	virtual ~UStaticMesh() ;

	virtual void Serialize(FArchive& Archive) override {};

	FName GetMeshKey() const { return MeshKey; }

	FStaticMeshLOD BuildImpostorLOD(const FStaticMeshLOD& SourceLOD, const FString& TexturePath, float ScreenSize);

	const TArray<FStaticMeshLOD>& GetLODs() const { return LODs; }

	const FStaticMeshLOD* GetLOD(uint32 LODIndex) const {
		if (LODIndex >= LODs.Num()) {
			return nullptr;
		}

		return &LODs[LODIndex];
	}

	FStaticMeshLOD* GetLOD(uint32 LODIndex) {
		if (LODIndex >= LODs.Num()) {
			return nullptr;
		}

		return &LODs[LODIndex];
	}

	void AddLOD(FStaticMeshLOD&& InLOD) {
		LODs.Add(std::move(InLOD));
	}

	uint32 GetLODCount() const {
		return LODs.Num();
	}

	// StaticMesh가 가지고 있는것 -> 해당 StaticMesh의 Default FMaterial
	const TArray<std::unique_ptr<FMaterial>>& GetDefaultMeshMaterials() const { return Materials; }
	const FMaterial* GetMaterial(uint32 SlotIndex) const
	{
		if (SlotIndex >= Materials.Num())
		{
			return nullptr;
		}
		return Materials[SlotIndex].get();
	}

	void SetMaterial(uint32 SlotIndex, std::unique_ptr<FMaterial> InMaterial)
	{
		if (SlotIndex >= Materials.Num())
		{
			Materials.resize(SlotIndex + 1);
		}
		Materials[SlotIndex] = std::move(InMaterial);
	}

	const FString& GetSourceFilePath() const { return SourceFilePath; }

	const FMeshResource* GetMeshResource(uint32 LODIndex = 0) const {
		const FStaticMeshLOD* LOD = GetLOD(LODIndex);
		return LOD ? LOD->MeshResource : nullptr;
	}

	FMeshResource* GetMeshResource(uint32 LODIndex = 0)
	{
		FStaticMeshLOD* LOD = GetLOD(LODIndex);
		return LOD ? LOD->MeshResource : nullptr;
	}

	const FVector& GetBoundsMin(uint32 LODIndex = 0) const
	{
		const FStaticMeshLOD* LOD = GetLOD(LODIndex);
		return LOD ? LOD->BoundsMin : FVector::Zero;
	}

	const FVector& GetBoundsMax(uint32 LODIndex = 0) const
	{
		const FStaticMeshLOD* LOD = GetLOD(LODIndex);
		return LOD ? LOD->BoundsMax : FVector::Zero;
	}

	bool HasBounds(uint32 LODIndex = 0) const
	{
		const FStaticMeshLOD* LOD = GetLOD(LODIndex);
		return LOD ? LOD->bHasBounds : false;
	}

	const TArray<FMeshSection>& GetSections(uint32 LODIndex = 0) const
	{
		const FStaticMeshLOD* LOD = GetLOD(LODIndex);
		return LOD ? LOD->Sections : EmptySections;
	}

	void SetSections(uint32 LODIndex, const TArray<FMeshSection>& InSections)
	{
		FStaticMeshLOD* LOD = GetLOD(LODIndex);
		if (!LOD)
		{
			return;
		}

		LOD->Sections = InSections;
	}

	void AddSection(uint32 LODIndex, uint32 InMaterialSlot, uint32 InStartIndex, uint32 InIndexCount)
	{
		FStaticMeshLOD* LOD = GetLOD(LODIndex);
		if (!LOD || !LOD->MeshResource)
		{
			return;
		}

		if (InStartIndex + InIndexCount > LOD->MeshResource->GetIndexCount())
		{
			return;
		}

		LOD->Sections.Add(FMeshSection{ InMaterialSlot, InStartIndex, InIndexCount });
	}

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

	TArray<std::unique_ptr<FMaterial>> Materials;

	TArray<FStaticMeshObjectInfo> Objects;

	TArray<FStaticMeshLOD> LODs;

	TArray<FMeshSection> EmptySections;
	//uint32 StartIndex;
	//uint32 IndexCount;

};