#include "pch.h"
#include "PassDrawBuilder.h"
#include "Engine/Resource/ResourceManager.h"
#include <algorithm>
#include <cmath>

void FPassDrawBuilder::BuildPassDraws(const FViewRenderData& ViewLayoutData, FPipelineStateCache* PipelineCache, FPassDrawList& OutPassDraws)
{
	OutPassDraws.Clear();
	m_ObjectIndexToCBIndex.clear();
	m_MaterialIdToCBIndex.clear();
	m_ReferencedMaterials.clear();

	const auto& ViewSnapshot = ViewLayoutData.View;
	const auto& Objects = ViewLayoutData.Objects;
	printf("ViewMode = %d\n", static_cast<int>(ViewSnapshot.ViewMode));
	auto GetOrCreateObjectCBIndex = [this](uint32 ObjectIndex) -> uint32 {
		auto It = m_ObjectIndexToCBIndex.find(ObjectIndex);
		if (It != m_ObjectIndexToCBIndex.end())	return It->second;

		uint32 NewIndex = static_cast<uint32>(m_ObjectIndexToCBIndex.size());
		m_ObjectIndexToCBIndex[ObjectIndex] = NewIndex;
		return NewIndex;
	};

	auto GetOrCreateMaterialCBIndex = [this](const FMaterial* Material) -> uint32 {
		uint32 MatId = Material ? Material->MaterialId : InvalidRenderId;
		auto It = m_MaterialIdToCBIndex.find(MatId);
		if (It != m_MaterialIdToCBIndex.end()) return It->second;

		uint32 NewIndex = static_cast<uint32>(m_MaterialIdToCBIndex.size());
		m_MaterialIdToCBIndex[MatId] = NewIndex;
		if (Material) {
			m_ReferencedMaterials.push_back(Material);
		}
		return NewIndex;
	};

	// 일반 primitives 처리
	for (const FPrimitiveRenderData& Prim : ViewLayoutData.Primitives) {
		if (Prim.ObjectIndex >= Objects.Num()) continue;
		const FRenderObjectData& ObjDatum = Objects[Prim.ObjectIndex];
		const FMaterial* Material = Prim.Material;

		FMeshPageBinding PageBinding = GResourceManager::GetInstance()->GetMeshPageBinding(Prim.Geometry.MeshPageId);

		uint32 ObjectCBIdx = GetOrCreateObjectCBIndex(Prim.ObjectIndex);
		uint32 MaterialCBIdx = GetOrCreateMaterialCBIndex(Material);
		uint32 DepthBucket = CalculateDepthBucket(ObjDatum.SortCenterWS, ViewSnapshot.ViewMatrix);

		EPrimitiveBlendMode BlendMode = Material ? Material->BlendMode : EPrimitiveBlendMode::Opaque;
		bool bIsTransparent = (BlendMode == EPrimitiveBlendMode::Additive);

		EPipelinePass TargetPass = bIsTransparent ? EPipelinePass::Additive : EPipelinePass::Opaque;

		FPipelineKey Key = MakePipelineKey(Material, PageBinding.VertexFormat, TargetPass, ViewSnapshot.ViewMode);
		const FPipelineState* State = PipelineCache->GetOrCreate(Key);
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
			OutPassDraws.AdditiveDraws.push_back(Draw);
		}
		else {
			OutPassDraws.OpaqueDraws.push_back(Draw);
		}

		const bool bSelected = (Prim.Flags & Primitive_Selected) != 0;
		const bool bAllowOutline = (Prim.Flags & Primitive_AllowOutline) != 0;

		if (bSelected && bAllowOutline) {
			FPipelineKey OutlineKey = MakePipelineKey(Material, PageBinding.VertexFormat, EPipelinePass::Outline, ViewSnapshot.ViewMode);
			const FPipelineState* OutlineState = PipelineCache->GetOrCreate(OutlineKey);
			if (OutlineState) {
				FPreparedDraw OutlineDraw = Draw;
				OutlineDraw.PipelineId = OutlineState->PipelineId;
				OutPassDraws.OutlineDraws.push_back(OutlineDraw);
			}
		}
	}

	// 기즈모 처리
	for (const FPrimitiveRenderData& GizmoPrim : ViewLayoutData.Gizmos)
	{
		if (GizmoPrim.ObjectIndex >= Objects.Num()) continue;

		const FRenderObjectData& ObjDatum = Objects[GizmoPrim.ObjectIndex];
		const FMaterial* Material = GizmoPrim.Material;

		FMeshPageBinding PageBinding = GResourceManager::GetInstance()->GetMeshPageBinding(GizmoPrim.Geometry.MeshPageId);

		uint32 ObjectCBIdx = GetOrCreateObjectCBIndex(GizmoPrim.ObjectIndex);
		uint32 MaterialCBIdx = GetOrCreateMaterialCBIndex(Material);
		uint32 DepthBucket = CalculateDepthBucket(ObjDatum.SortCenterWS, ViewSnapshot.ViewMatrix);

		FPipelineKey Key = MakePipelineKey(Material, PageBinding.VertexFormat, EPipelinePass::Gizmo, ViewSnapshot.ViewMode);
		const FPipelineState* State = PipelineCache->GetOrCreate(Key);
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

		OutPassDraws.GizmoDraws.push_back(Draw);
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
