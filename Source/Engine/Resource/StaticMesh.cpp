#include <pch.h>
#include "StaticMesh.h"
#include "ResourceManager.h"
#include "Engine/Resource/TextureResource.h"
#include "Core/Util/File.h"
#include "Engine/Log.h"
#include <stdexcept>
#include "../../../ThirdParty/meshoptimizer/meshoptimizer.h"

UStaticMesh::~UStaticMesh() = default;
namespace
{
	TArray<uint32> GenerateSimplifiedIndices(const TArray<FVector>& Positions, const TArray<uint32>& SourceIndices, float TriangleRatio)
	{
		TArray<uint32> Result;

		if (Positions.Num() == 0 || SourceIndices.Num() < 3)
		{
			return Result;
		}

		const size_t SourceIndexCount = static_cast<size_t>(SourceIndices.Num());

		size_t TargetIndexCount = static_cast<size_t>(SourceIndexCount * TriangleRatio);

		TargetIndexCount -= TargetIndexCount % 3;

		if (TargetIndexCount < 3)
		{
			TargetIndexCount = 3;
		}

		if (TargetIndexCount >= SourceIndexCount)
		{
			Result = SourceIndices;
			return Result;
		}

		Result.resize(SourceIndexCount);

		float ResultError = 0.0f;

		const size_t SimplifiedIndexCount =
			meshopt_simplify(
				Result.GetData(),
				SourceIndices.GetData(),
				SourceIndexCount,

				reinterpret_cast<const float*>(Positions.GetData()),
				static_cast<size_t>(Positions.Num()),
				sizeof(FVector),

				TargetIndexCount,
				0.01f,
				meshopt_SimplifySparse,
				&ResultError
			);

		Result.resize(SimplifiedIndexCount);

		return Result;
	}

	FStaticMeshLOD BuildLOD(
		GResourceManager* RM,
		const FStaticMeshLOD& SourceLOD,
		const FStaticMeshData& MeshData,
		const FString& ResourceKey,
		float TriangleRatio,
		float ScreenSize)
	{
		FStaticMeshLOD LOD;

		if (!RM || !SourceLOD.MeshResource)
		{
			return LOD;
		}

		const TArray<FVector>& Positions =
			SourceLOD.MeshResource->GetPositions();

		TArray<uint32> LODIndices;

		for (const FMeshSection& SourceSection : MeshData.Sections)
		{
			if (SourceSection.IndexCount == 0)
			{
				continue;
			}

			if (SourceSection.FirstIndex + SourceSection.IndexCount >
				static_cast<uint32>(MeshData.Indices.Num()))
			{
				continue;
			}

			TArray<uint32> SectionIndices;
			SectionIndices.Reserve(SourceSection.IndexCount);

			for (uint32 Index = 0; Index < SourceSection.IndexCount; ++Index)
			{
				SectionIndices.Add(
					MeshData.Indices[
						SourceSection.FirstIndex + Index
					]
				);
			}

			TArray<uint32> SimplifiedIndices =
				GenerateSimplifiedIndices(
					Positions,
					SectionIndices,
					TriangleRatio
				);

			if (SimplifiedIndices.Num() < 3)
			{
				continue;
			}

			FMeshSection LODSection{};
			LODSection.MaterialIndex = SourceSection.MaterialIndex;
			LODSection.FirstIndex =
				static_cast<uint32>(LODIndices.Num());
			LODSection.IndexCount =
				static_cast<uint32>(SimplifiedIndices.Num());

			LOD.Sections.Add(LODSection);

			for (uint32 Index : SimplifiedIndices)
			{
				LODIndices.Add(Index);
			}
		}

		if (LODIndices.Num() == 0)
		{
			return LOD;
		}

		LOD.MeshResource =
			RM->CreateStaticMeshResource(
				ResourceKey,
				MeshData.Vertices,
				LODIndices
			);

		LOD.BoundsMin = SourceLOD.BoundsMin;
		LOD.BoundsMax = SourceLOD.BoundsMax;
		LOD.bHasBounds = SourceLOD.bHasBounds;
		LOD.ScreenSize = ScreenSize;

		return LOD;
	}
}
void UStaticMesh::BuildFromMeshData(const FStaticMeshData& MeshData)
{
	GResourceManager* RM = GResourceManager::GetInstance();

	LODs.Empty();

	FStaticMeshLOD LOD0;
	LOD0.MeshResource = RM->CreateStaticMeshResource(MeshData.PathFileName, MeshData.Vertices, MeshData.Indices);

	if (!LOD0.MeshResource) return;

	LOD0.Sections = MeshData.Sections;
	LOD0.BoundsMin = MeshData.BoundsMin;
	LOD0.BoundsMax = MeshData.BoundsMax;
	LOD0.bHasBounds = true;
	LOD0.ScreenSize = 1.0f;

	LODs.Add(std::move(LOD0));
	
	{
		const FStaticMeshLOD* SourceLOD = GetLOD(0);

		FStaticMeshLOD LOD1 =
			BuildLOD(
				RM,
				*SourceLOD,
				MeshData,
				MeshData.PathFileName + "_LOD1",
				0.3f,
				0.5f
			);

		if (LOD1.MeshResource)
		{
			LODs.Add(std::move(LOD1));
		}
	}

	{
		const FStaticMeshLOD* SourceLOD = GetLOD(0);

		FStaticMeshLOD LOD2 =
			BuildLOD(
				RM,
				*SourceLOD,
				MeshData,
				MeshData.PathFileName + "_LOD2",
				0.05f,
				0.15f
			);

		if (LOD2.MeshResource)
		{
			LODs.Add(std::move(LOD2));
		}
	}


	for (const FStaticMeshMaterial CPUMaterial : MeshData.Materials) {
		ID3D11ShaderResourceView* SRV = nullptr;
		FString TexturePath = File::PathToUtf8(CPUMaterial.DiffuseTexturePath);

		if (!CPUMaterial.DiffuseTexturePath.empty()) {
			try {
				if (FTextureResource* Tex = RM->GetOrLoadTexture(TexturePath)) {
					SRV = Tex->GetSRV();
				}
			}
			catch (const std::exception& Error) {
				UE_LOG("[StaticMesh] Texture load failed: {} ({})", TexturePath, Error.what());
			}
		}

		if (!SRV) {
			TexturePath = "Assets/Textures/WhiteTexture.png";

			if (FTextureResource* WhiteTex = RM->GetOrLoadTexture(TexturePath)) {
				SRV = WhiteTex->GetSRV();
			}
		}

		FMaterial GPUMaterial = RM->CreateStaticMeshMaterial(SRV, TexturePath, CPUMaterial.DiffuseTextureOptions.bClamp);

		Materials.Add(std::make_unique<FMaterial>(std::move(GPUMaterial)));
	}
	SourceFilePath = MeshData.PathFileName;
	MeshKey = FName(MeshData.PathFileName);

	Objects = MeshData.Objects;
}