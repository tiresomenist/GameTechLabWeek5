#include "pch.h"
#include "StaticMeshComponent.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Resource/TextureResource.h"
#include "Engine/Resource/ResourceManager.h"
#include <stdexcept>
#include <cassert>
#include "Engine/Actor/Actor.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Log.h"
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
			RefreshUVScrollTick();
		}
	}
}

// 수집 단계의 월드 행렬과 중심을 재사용하여 LOD 및 섹션별 요청을 생성합니다.
void UStaticMeshComponent::CreateRenderData(TArray<FPrimitiveRenderData>& ComponentRenderData,
	const FPrimitiveRenderContext& Context, bool bSelected)
{
	if (!CachedMesh || !Context.Camera) return;

	const TArray<FStaticMeshLOD>& LODs = CachedMesh->GetLODs();
	const uint32 LODCount = static_cast<uint32>(LODs.Num());
	if (LODCount == 0) return;

	const FMatrix& World = Context.World;
	uint32 LODIndex = 0;
	if (LODCount > 1)
	{
		const FVector WorldCenter = Context.bHasWorldCenter ? Context.WorldCenter
			: World.TransformPosition((LODs[0].BoundsMin + LODs[0].BoundsMax) * 0.5f);
		const FVector ToCamera = WorldCenter - Context.CameraLocation;

		const float DistanceSquared = ToCamera.LengthSquared();
		if (DistanceSquared >= 100.0f)
			LODIndex = 1;
		if (LODCount > 2 && DistanceSquared >= 900.0f)
			LODIndex = 2;
		if (LODCount > 3 && DistanceSquared >= 3600.0f)
			LODIndex = 3;
	}

	const FStaticMeshLOD& LOD = LODs[LODIndex];
	if (!LOD.MeshResource) return;

	const FMeshAllocation& Allocation = LOD.MeshResource->GetAllocation();
	if (Allocation.MeshPageId == InvalidRenderId) return;

	// 임포스터 정보는 Section과 무관하므로 한 번만 계산
	FVector ImpostorCenterWS{};
	FVector4 ImpostorSize{ 1.0f, 1.0f, 0.0f, 0.0f };
	FVector4 ImpostorUV{ 1.0f, 1.0f, 0.0f, 0.0f };

	if (LOD.bImpostor)
	{
		ImpostorCenterWS = World.TransformPosition(LOD.Impostor.Pivot);

		const FVector WidthPointWS =
			World.TransformPosition(
				LOD.Impostor.Pivot +
				FVector(LOD.Impostor.Width * 0.5f, 0.0f, 0.0f));

		const FVector HeightPointWS =
			World.TransformPosition(
				LOD.Impostor.Pivot +
				FVector(0.0f, 0.0f, LOD.Impostor.Height * 0.5f));

		const float WorldWidth =
			(WidthPointWS - ImpostorCenterWS).Length() * 2.0f;

		const float WorldHeight =
			(HeightPointWS - ImpostorCenterWS).Length() * 2.0f;

		ImpostorSize = FVector4(
			(std::max)(WorldWidth, 0.01f),
			(std::max)(WorldHeight, 0.01f),
			0.0f,
			0.0f);

		const FVector ToCamera =
			Context.CameraLocation - ImpostorCenterWS;

		const float Yaw =
			std::atan2(ToCamera.Y, ToCamera.X);

		float NomalizedYaw =
			Yaw / (2.0f * PI);

		if (NomalizedYaw < 0.0f)
			NomalizedYaw += 1.0f;

		const int32 ViewCountX =
			static_cast<int32>(LOD.Impostor.ViewCountX);

		int32 ViewX = static_cast<int32>(NomalizedYaw * static_cast<float>(ViewCountX) + 0.5f);

		if (ViewX >= ViewCountX)
			ViewX = 0;

		const float Distance = ToCamera.Length();
		const float InvDistance =
			Distance > 0.0001f ? 1.0f / Distance : 0.0f;

		const float Pitch =
			std::asin(
				std::clamp(
					ToCamera.Z * InvDistance,
					-1.0f,
					1.0f));

		constexpr float PitchMin = -60.0f * PI / 180.0f;
		constexpr float PitchMax = 60.0f * PI / 180.0f;

		const float ClampedPitch =
			std::clamp(Pitch, PitchMin, PitchMax);

		const float Pitch01 =
			(ClampedPitch - PitchMin) /
			(PitchMax - PitchMin);

		const int32 ViewCountY =
			static_cast<int32>(LOD.Impostor.ViewCountY);

		int32 ViewY = static_cast<int32>((1.0f - Pitch01) * static_cast<float>(ViewCountY - 1) + 0.5f);

		ViewY = std::clamp(ViewY, 0, ViewCountY - 1);

		const float UVScaleX =
			1.0f / static_cast<float>(ViewCountX);

		const float UVScaleY =
			1.0f / static_cast<float>(ViewCountY);

		ImpostorUV = FVector4(
			UVScaleX,
			UVScaleY,
			ViewX * UVScaleX,
			ViewY * UVScaleY);
	}

	for (const FMeshSection& Section : LOD.Sections)
	{
		if (Section.IndexCount == 0) continue;

		const FMaterial* SectionMaterial =
			GetMaterial(Section.MaterialIndex);

		if (!SectionMaterial) continue;

		assert(SectionMaterial->MaterialId != InvalidRenderId);
		assert(Section.FirstIndex <= Allocation.IndexCount);
		assert(Section.IndexCount <=
			Allocation.IndexCount - Section.FirstIndex);

		FPrimitiveRenderData& Data = ComponentRenderData.GetVector().emplace_back();
		Data.Geometry.MeshPageId = Allocation.MeshPageId;
		Data.Geometry.FirstIndex =
			Allocation.FirstIndex + Section.FirstIndex;
		Data.Geometry.IndexCount = Section.IndexCount;
		Data.Geometry.BaseVertex = Allocation.BaseVertex;
		Data.Material = SectionMaterial;

		if (LOD.bImpostor)
		{
			Data.bImpostor = true;
			Data.ImpostorCenterWS = ImpostorCenterWS;
			Data.ImpostorSize = ImpostorSize;
			Data.ImpostorUV = ImpostorUV;
			Data.ImpostorCameraLocation = Context.CameraLocation;
		}

		Data.Flags = Primitive_AllowOutline;
		if (bSelected)
			Data.Flags |= Primitive_Selected;

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

//Todo: Picking cache
void UStaticMeshComponent::OnWorldBoundsChanged()
{
	//Todo: Picking cache
	Super::OnWorldBoundsChanged();

	AActor* Owner = GetOwner();
	UScene* Scene = Owner ? Owner->GetScene() : nullptr;
	if (!Scene) return;

	// 기존 BVH API가 비const 포인터를 받지만, 컴포넌트 자체를 수정하지는 않습니다.
	Scene->UpdateBVH(const_cast<UStaticMeshComponent*>(this));
}
