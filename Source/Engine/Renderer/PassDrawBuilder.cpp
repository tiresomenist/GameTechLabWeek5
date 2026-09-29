#include "pch.h"
#include "PassDrawBuilder.h"
#include "Engine/Resource/ResourceManager.h"
#include <algorithm>
#include <cmath>

void FPassDrawBuilder::BuildPassDraws(const FViewRenderData& ViewLayoutData, FPipelineStateCache* PipelineCache, 
	FPassDrawList& OutPassDraws, const TArray<uint32>& PrimitiveVisibility)
{
	OutPassDraws.Clear();
	m_MaterialIdToCBIndex.Empty();
	m_ReferencedMaterials.Empty();
	ResetObjectMapping(ViewLayoutData.Objects.Num());

	const auto& ViewSnapshot = ViewLayoutData.View;
	const auto& Objects = ViewLayoutData.Objects;
	FPipelineKey LastPipelineKey{};
	const FPipelineState* LastPipelineState = nullptr;

	auto FindPipeline = [&](const FPipelineKey& Key) -> const FPipelineState* {
		if (LastPipelineState && LastPipelineKey == Key) return LastPipelineState;

		const FPipelineState* State = PipelineCache->GetOrCreate(Key);
		if (State) {
			LastPipelineKey = Key;
			LastPipelineState = State;
		}
		return State;
	};

	auto GetOrCreateMaterialCBIndex = [this](const FMaterial* Material) -> uint32 {
		if (const uint32* Existing = m_MaterialIdToCBIndex.Find(Material->MaterialId))
		{
			return *Existing;
		}
		const uint32 NewIndex =	static_cast<uint32>(m_ReferencedMaterials.Num());
		// 배열 위치와 CB 인덱스를 동일하게 유지한다.
		m_ReferencedMaterials.Add(Material);
		m_MaterialIdToCBIndex.Add(Material->MaterialId, NewIndex);
		return NewIndex;
	};

	// 일반 primitives 처리
	for (int32 Index = 0; Index < ViewLayoutData.Primitives.Num(); ++Index)
	{
		// 페이지 조회와 CB 매핑보다 먼저 제외한다.
		if (PrimitiveVisibility[Index] == 0) continue;

		const FPrimitiveRenderData& Prim = ViewLayoutData.Primitives[Index];
		if (Prim.ObjectIndex >= Objects.Num()) continue;
		const FRenderObjectData& ObjDatum = Objects[Prim.ObjectIndex];
		const FMaterial* Material = Prim.Material;
		if (!Material || Material->MaterialId == InvalidRenderId) continue;
		FMeshPageBinding PageBinding = GResourceManager::GetInstance()->GetMeshPageBinding(Prim.Geometry.MeshPageId);

		uint32 ObjectCBIdx = GetOrCreateObjectCBIndex(Prim.ObjectIndex);
		uint32 MaterialCBIdx = GetOrCreateMaterialCBIndex(Material);
		uint32 DepthBucket = CalculateDepthBucket(ObjDatum.SortCenterWS, ViewSnapshot.ViewMatrix);

		EPrimitiveBlendMode BlendMode = Material ? Material->BlendMode : EPrimitiveBlendMode::Opaque;
		bool bIsTransparent = (BlendMode == EPrimitiveBlendMode::Additive);

		EPipelinePass TargetPass = bIsTransparent ? EPipelinePass::Additive : EPipelinePass::Opaque;

		FPipelineKey Key = MakePipelineKey(Material, PageBinding.VertexFormat, TargetPass, ViewSnapshot.ViewMode);
		const FPipelineState* State = FindPipeline(Key);
		if (!State) continue;

		FPreparedDraw Draw;
		Draw.Source = &Prim;
		Draw.PipelineId = State->PipelineId;
		Draw.ObjectConstantIndex = ObjectCBIdx;
		Draw.MaterialConstantIndex = MaterialCBIdx;
		Draw.DepthBucket = DepthBucket;
		Draw.MaterialId = Material ? Material->MaterialId : InvalidRenderId;
		Draw.MeshPageId = Prim.Geometry.MeshPageId;
		Draw.ObjectIndex = Prim.ObjectIndex;
		Draw.SortKey =
			(static_cast<uint64>(Draw.PipelineId) << 48) |
			(static_cast<uint64>(Draw.MaterialId) << 32) |
			(static_cast<uint64>(Draw.DepthBucket) << 16) |
			(static_cast<uint64>(Draw.MeshPageId));

		if (bIsTransparent) {
			OutPassDraws.AdditiveDraws.Add(Draw);
		}
		else {
			OutPassDraws.OpaqueDraws.Add(Draw);
		}

		const bool bSelected = (Prim.Flags & Primitive_Selected) != 0;
		const bool bAllowOutline = (Prim.Flags & Primitive_AllowOutline) != 0;

		if (bSelected && bAllowOutline) {
			FPipelineKey OutlineKey = MakePipelineKey(Material, PageBinding.VertexFormat, EPipelinePass::Outline, ViewSnapshot.ViewMode);
			const FPipelineState* OutlineState = FindPipeline(OutlineKey);
			if (OutlineState) {
				FPreparedDraw OutlineDraw = Draw;
				OutlineDraw.PipelineId = OutlineState->PipelineId;
				OutPassDraws.OutlineDraws.Add(OutlineDraw);
			}
		}
	}


	// 기즈모 처리
	for (const FPrimitiveRenderData& GizmoPrim : ViewLayoutData.Gizmos)
	{
		if (GizmoPrim.ObjectIndex >= Objects.Num()) continue;

		const FRenderObjectData& ObjDatum = Objects[GizmoPrim.ObjectIndex];
		const FMaterial* Material = GizmoPrim.Material;
		if (!Material || Material->MaterialId == InvalidRenderId) continue;
		FMeshPageBinding PageBinding = GResourceManager::GetInstance()->GetMeshPageBinding(GizmoPrim.Geometry.MeshPageId);

		uint32 ObjectCBIdx = GetOrCreateObjectCBIndex(GizmoPrim.ObjectIndex);
		uint32 MaterialCBIdx = GetOrCreateMaterialCBIndex(Material);
		uint32 DepthBucket = CalculateDepthBucket(ObjDatum.SortCenterWS, ViewSnapshot.ViewMatrix);
		FPipelineKey Key;
		if (GizmoPrim.Topology == D3D10_PRIMITIVE_TOPOLOGY_LINELIST) {
			Key = MakePipelineKey(Material, PageBinding.VertexFormat, EPipelinePass::Line, ViewSnapshot.ViewMode);
		}
		else {
			Key = MakePipelineKey(Material, PageBinding.VertexFormat, EPipelinePass::Gizmo, ViewSnapshot.ViewMode);
		}
		const FPipelineState* State = FindPipeline(Key);
		if (!State) continue;

		FPreparedDraw Draw;
		Draw.Source = &GizmoPrim;
		Draw.PipelineId = State->PipelineId;
		Draw.ObjectConstantIndex = ObjectCBIdx;
		Draw.MaterialConstantIndex = MaterialCBIdx;
		Draw.DepthBucket = DepthBucket;
		Draw.MaterialId = Material ? Material->MaterialId : InvalidRenderId;
		Draw.MeshPageId = GizmoPrim.Geometry.MeshPageId;
		Draw.ObjectIndex = GizmoPrim.ObjectIndex;
		Draw.SortKey =
			(static_cast<uint64>(Draw.PipelineId) << 48) |
			(static_cast<uint64>(Draw.MaterialId) << 32) |
			(static_cast<uint64>(Draw.DepthBucket) << 16) |
			(static_cast<uint64>(Draw.MeshPageId));

		OutPassDraws.GizmoDraws.Add(Draw);
	}
}

FPipelineKey FPassDrawBuilder::MakePipelineKey(const FMaterial* Material, EVertexFormat VertexFormat, EPipelinePass Pass, EViewModeIndex ViewMode) const
{
	FPipelineKey Key;
	Key.Pass = Pass;
	Key.VertexFormat = VertexFormat;
	Key.ViewMode = ViewMode;

	if (Material && Material->Shader) {
		Key.Shader = Material->Shader;
		Key.BlendMode = Material->BlendMode;
		Key.bTwoSided = Material->bTwoSided;
	}
	else {
		Key.Shader = nullptr;
		Key.BlendMode = EPrimitiveBlendMode::Opaque;
		Key.bTwoSided = false;
	}

	return Key;
}

uint32 FPassDrawBuilder::CalculateDepthBucket(const FVector& SortCenterWS, const FMatrix& ViewMatrix, float NearZ, float FarZ) const
{

	FVector4 ViewPos = FVector4(SortCenterWS, 1) * ViewMatrix;
	float ViewZ = ViewPos.Z;

	float NormalizedDepth = (ViewZ - NearZ) / (FarZ - NearZ);
	NormalizedDepth = std::clamp(NormalizedDepth, 0.0f, 1.0f);

	return static_cast<uint32>(NormalizedDepth * 65535.0f);
}

void FPassDrawBuilder::ResetObjectMapping(size_t ObjectCount)
{
	ObjectIndexToCBIndex.SetNum(ObjectCount);
	std::fill(ObjectIndexToCBIndex.begin(), ObjectIndexToCBIndex.end(), InvalidRenderId);

	ReferenceObjectIndices.Empty();
}

uint32 FPassDrawBuilder::GetOrCreateObjectCBIndex(uint32 ObjectIndex)
{
	uint32& CBIndex = ObjectIndexToCBIndex[ObjectIndex];
	if (CBIndex != InvalidRenderId) return CBIndex;
	const uint32 NewIndex = static_cast<uint32>(ReferenceObjectIndices.Num());
	ReferenceObjectIndices.Add(ObjectIndex);
	CBIndex = NewIndex;
	return CBIndex;
}
