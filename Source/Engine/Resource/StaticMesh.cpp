#include <pch.h>
#include "StaticMesh.h"
#include "ResourceManager.h"
#include "Engine/Resource/TextureResource.h"
#include "Core/Util/File.h"
#include "Engine/Log.h"
#include <stdexcept>
#include "../../../ThirdParty/meshoptimizer/meshoptimizer.h"
#include "Engine/Renderer/ImpostorBaker.h"
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

UStaticMesh::~UStaticMesh() = default;

FStaticMeshLOD UStaticMesh::BuildImpostorLOD(const FStaticMeshLOD& SourceLOD, const FString& TexturePath, float ScreenSize)
{
	FStaticMeshLOD LOD;

	GResourceManager* RM = GResourceManager::GetInstance();
	if (!RM) return LOD;

	FTextureResource* AtlasTexture = nullptr;

	try {
		AtlasTexture = RM->GetOrLoadTexture(TexturePath);
	}
	catch (const std::exception& Error) {
		UE_LOG("[StaticMesh] Impostor texture load failed: {} ({})", TexturePath, Error.what());
		return LOD;
	}

	if (!AtlasTexture || !AtlasTexture->GetSRV()) return LOD;

	static const FVertexPNCT QuadVertices[4] = {
		{
			-0.5f, 0.5f, 0.0f,
			 0.0f, 0.0f, 1.0f,
			 1.0f, 1.0f, 1.0f, 1.0f,
			 0.0f, 0.0f
		},
		{
			0.5f, 0.5f, 0.0f,
			0.0f, 0.0f, 1.0f,
			1.0f, 1.0f, 1.0f, 1.0f,
			1.0f, 0.0f
		},
		{
			-0.5f, -0.5f, 0.0f,
			 0.0f,  0.0f, 1.0f,
			 1.0f,  1.0f, 1.0f, 1.0f,
			 0.0f,  1.0f
		},
		{
			0.5f, -0.5f, 0.0f,
			0.0f,  0.0f, 1.0f,
			1.0f,  1.0f, 1.0f, 1.0f,
			1.0f,  1.0f
		}
	};

	static const uint32 QuadIndices[6] = { 0,1,2,2,1,3 };

	const FName QuadName("Impostor.SharedQuad");

	LOD.MeshResource = RM->CreateStaticMeshResource(QuadName, std::span<const FVertexPNCT>(QuadVertices, 4), std::span<const uint32>(QuadIndices, 6));
	
	if (!LOD.MeshResource) return FStaticMeshLOD{};

	FMeshSection LODSection{};
	LODSection.MaterialIndex = InvalidRenderId;
	LODSection.FirstIndex = 0;
	LODSection.IndexCount = 6;

	LOD.Sections.Add(LODSection);

	LOD.BoundsMin = SourceLOD.BoundsMin;
	LOD.BoundsMax = SourceLOD.BoundsMax;
	LOD.bHasBounds = SourceLOD.bHasBounds;

	LOD.ScreenSize = ScreenSize;
	LOD.bImpostor = true;

	LOD.Impostor.TexturePath = TexturePath;
	LOD.Impostor.ViewCountX = 8;
	LOD.Impostor.ViewCountY = 4;

	if (SourceLOD.bHasBounds) {
		const FVector Center = (SourceLOD.BoundsMin + SourceLOD.BoundsMax) * 0.5f;

		const FVector Extent = (SourceLOD.BoundsMax - SourceLOD.BoundsMin) * 0.5f;

		LOD.Impostor.Pivot = Center;

		const float Diameter = (Extent * 2.0f).Length();

		LOD.Impostor.Width = Diameter * 1.05f;

		LOD.Impostor.Height = Diameter * 1.05f;
	}

	return LOD;
}

void UStaticMesh::BuildFromMeshData(const FStaticMeshData& MeshData)
{
	GResourceManager* RM = GResourceManager::GetInstance();

	LODs.Empty();
	Materials.Empty();

	if (!RM)
	{
		return;
	}

	// LOD0
	FStaticMeshLOD LOD0;

	LOD0.MeshResource =
		RM->CreateStaticMeshResource(
			MeshData.PathFileName,
			MeshData.Vertices,
			MeshData.Indices
		);

	if (!LOD0.MeshResource)
	{
		return;
	}

	LOD0.Sections = MeshData.Sections;
	LOD0.BoundsMin = MeshData.BoundsMin;
	LOD0.BoundsMax = MeshData.BoundsMax;
	LOD0.bHasBounds = true;
	LOD0.ScreenSize = 1.0f;

	LODs.Add(std::move(LOD0));

	// LOD1
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

	// LOD2
	{
		const FStaticMeshLOD* SourceLOD = GetLOD(0);

		FStaticMeshLOD LOD2 =
			BuildLOD(
				RM,
				*SourceLOD,
				MeshData,
				MeshData.PathFileName + "_LOD2",
				0.08f,
				0.15f
			);

		if (LOD2.MeshResource)
		{
			LODs.Add(std::move(LOD2));
		}
	}

	// 기본 Material 생성
	for (const FStaticMeshMaterial CPUMaterial : MeshData.Materials)
	{
		ID3D11ShaderResourceView* SRV = nullptr;

		FString TexturePath =
			File::PathToUtf8(
				CPUMaterial.DiffuseTexturePath
			);

		if (!CPUMaterial.DiffuseTexturePath.empty())
		{
			try
			{
				if (FTextureResource* Tex =
					RM->GetOrLoadTexture(TexturePath))
				{
					SRV = Tex->GetSRV();
				}
			}
			catch (const std::exception& Error)
			{
				UE_LOG(
					"[StaticMesh] Texture load failed: {} ({})",
					TexturePath,
					Error.what()
				);
			}
		}

		// Diffuse Texture를 못 찾았으면 WhiteTexture 사용
		if (!SRV)
		{
			TexturePath =
				"Assets/Textures/WhiteTexture.png";

			if (FTextureResource* WhiteTex =
				RM->GetOrLoadTexture(TexturePath))
			{
				SRV = WhiteTex->GetSRV();
			}
		}

		FMaterial GPUMaterial =
			RM->CreateStaticMeshMaterial(
				SRV,
				TexturePath,
				CPUMaterial.DiffuseTextureOptions.bClamp
			);

		Materials.Add(
			std::make_unique<FMaterial>(
				std::move(GPUMaterial)
			)
		);
	}

	// Impostor LOD3
	{
		const FString ImpostorTexturePath =
			MeshData.PathFileName + "_Impostor.png";

		// 먼저 Atlas 생성
		FImpostorBaker Baker;

		if (Baker.Bake(this, ImpostorTexturePath))
		{
			FTextureResource* ImpostorTexture = nullptr;

			try
			{
				ImpostorTexture =
					RM->GetOrLoadTexture(
						ImpostorTexturePath
					);
			}
			catch (...)
			{
				ImpostorTexture = nullptr;
			}

			if (ImpostorTexture &&
				ImpostorTexture->GetSRV())
			{
				// Impostor Material 생성
				FMaterial ImpostorMaterial =
					RM->CreateImpostorMaterial(
						ImpostorTexture->GetSRV(),
						ImpostorTexturePath
					);

				Materials.Add(
					std::make_unique<FMaterial>(
						std::move(ImpostorMaterial)
					)
				);

				const uint32 ImpostorMaterialIndex =
					static_cast<uint32>(
						Materials.Num() - 1
						);

				// LOD3 생성
				const FStaticMeshLOD* SourceLOD =
					GetLOD(0);

				FStaticMeshLOD LOD3 =
					BuildImpostorLOD(
						*SourceLOD,
						ImpostorTexturePath,
						0.05f
					);

				if (LOD3.bImpostor &&
					LOD3.MeshResource &&
					!LOD3.Sections.IsEmpty())
				{
					LOD3.Impostor.MaterialIndex =
						ImpostorMaterialIndex;

					LOD3.Sections[0].MaterialIndex =
						ImpostorMaterialIndex;

					LODs.Add(
						std::move(LOD3)
					);
				}
			}
		}
	}

	SourceFilePath =
		MeshData.PathFileName;

	MeshKey =
		FName(MeshData.PathFileName);

	Objects =
		MeshData.Objects;
}