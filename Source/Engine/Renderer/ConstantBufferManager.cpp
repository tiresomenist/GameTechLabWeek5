#include "pch.h"
#include "ConstantBufferManager.h"
#include <cassert>
void FConstantBufferManager::UploadObjectConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const FViewRenderData& ViewLayoutData, const TArray<uint32>& ObjectIndexToCBIndices)
{
	m_ObjectCBAllocations.SetNum(ObjectIndexToCBIndices.Num());

	const auto& Objects = ViewLayoutData.Objects;
	const FMatrix& ViewProj = ViewLayoutData.View.ViewProjection;

	for (int32 CBIndex = 0; CBIndex < ObjectIndexToCBIndices.Num(); ++CBIndex)
	{
		const uint32 ObjectIndex = ObjectIndexToCBIndices[CBIndex];

		FObjectConstants Constants{};
		Constants.World = Objects[ObjectIndex].World;
		Constants.ViewProjection = ViewProj;

		Constants.ImpostorCenterWS =
			FVector4(
				Objects[ObjectIndex].ImpostorCenterWS.X,
				Objects[ObjectIndex].ImpostorCenterWS.Y,
				Objects[ObjectIndex].ImpostorCenterWS.Z,
				1.0f
			);

		Constants.ImpostorSize = Objects[ObjectIndex].ImpostorSize;

		Constants.ImpostorUV = Objects[ObjectIndex].ImpostorUV;

		Constants.ImpostorCameraLocation =
			FVector4(
				Objects[ObjectIndex].ImpostorCameraLocation.X,
				Objects[ObjectIndex].ImpostorCameraLocation.Y,
				Objects[ObjectIndex].ImpostorCameraLocation.Z,
				1.0f
			);

		// 드로우 요청의 ObjectConstantIndex와 같은 위치에 기록한다.
		m_ObjectCBAllocations[CBIndex] = CBRing->AllocateAndUpload(
			Context, &Constants, sizeof(Constants));
	}
}

void FConstantBufferManager::UploadMaterialConstants(ID3D11DeviceContext* Context, FConstantBufferRing* CBRing, const TArray<const FMaterial*>& ReferencedMaterials)
{
	m_MaterialCBAllocations.SetNum(ReferencedMaterials.Num());

	for (int32 CBIndex = 0; CBIndex < ReferencedMaterials.Num(); ++CBIndex)
	{
		const FMaterial& Material = *ReferencedMaterials[CBIndex];

		FTextureDrawConstants Constants{};
		Constants.DiffuseColor = Material.DiffuseColor;
		Constants.AlphaCutoff = Material.AlphaCutoff;
		Constants.UV.Scale = Material.UVScale;
		Constants.UV.Offset = Material.UVOffset;

		// Tick에서 갱신된 UVOffset을 이번 View의 상수에 반영한다.
		m_MaterialCBAllocations[CBIndex] = CBRing->AllocateAndUpload(Context, &Constants, sizeof(Constants));
	}
}

const FCBRangeAllocation& FConstantBufferManager::GetObjectCBRange(uint32 ObjectCBIndex) const
{
	return m_ObjectCBAllocations[ObjectCBIndex];
}

const FCBRangeAllocation& FConstantBufferManager::GetMaterialCBRange(uint32 MaterialCBIndex) const
{
	return m_MaterialCBAllocations[MaterialCBIndex];
}

void FConstantBufferManager::Clear()
{
	m_ObjectCBAllocations.Empty();
	m_MaterialCBAllocations.Empty();
}
