#include "pch.h"
#include "Engine/Util/DebugCpuStats.h"
#include "PassDrawBuilder.h"
#include "Engine/Resource/ResourceManager.h"
#include <algorithm>
#include <cmath>

void FPassDrawBuilder::BuildPassDraws(
	const FViewRenderData& ViewLayoutData,
	FPipelineStateCache* PipelineCache,
	FPassDrawList& OutPassDraws,
	const TArray<uint32>& PrimitiveVisibility)
{
	FScopedDebugCpuTime CpuTime(EDebugCpuStat::BuildPasses);

	OutPassDraws.Clear();
	m_MaterialIdToCBIndex.Empty();
	m_ReferencedMaterials.Empty();
	ResetObjectMapping(ViewLayoutData.Objects.Num());

	const auto& ViewSnapshot = ViewLayoutData.View;
	const auto& Objects = ViewLayoutData.Objects;

	const uint32 PrimitiveCount =
		static_cast<uint32>(ViewLayoutData.Primitives.Num());

	const uint32 GizmoCount =
		static_cast<uint32>(ViewLayoutData.Gizmos.Num());

	// Clear() 이후 capacity 재사용.
	OutPassDraws.OpaqueDraws.Reserve(PrimitiveCount);
	OutPassDraws.AdditiveDraws.Reserve(PrimitiveCount);
	OutPassDraws.OutlineDraws.Reserve(PrimitiveCount);
	OutPassDraws.GizmoDraws.Reserve(GizmoCount);

	FPipelineKey LastPipelineKey{};
	const FPipelineState* LastPipelineState = nullptr;

	auto FindPipeline =
		[&](const FPipelineKey& Key) -> const FPipelineState*
		{
			if (LastPipelineState && LastPipelineKey == Key)
				return LastPipelineState;

			const FPipelineState* State =
				PipelineCache->GetOrCreate(Key);

			if (State)
			{
				LastPipelineKey = Key;
				LastPipelineState = State;
			}

			return State;
		};

	const FMaterial* LastMaterial = nullptr;
	uint32 LastMaterialCBIndex = InvalidRenderId;

	auto GetOrCreateMaterialCBIndex =
		[this, &LastMaterial, &LastMaterialCBIndex]
		(const FMaterial* Material) -> uint32
		{
			if (Material == LastMaterial)
				return LastMaterialCBIndex;

			uint32 MaterialCBIndex;

			if (const uint32* Existing =
				m_MaterialIdToCBIndex.Find(Material->MaterialId))
			{
				MaterialCBIndex = *Existing;
			}
			else
			{
				MaterialCBIndex =
					static_cast<uint32>(m_ReferencedMaterials.Num());

				m_ReferencedMaterials.Add(Material);
				m_MaterialIdToCBIndex.Add(
					Material->MaterialId,
					MaterialCBIndex);
			}

			LastMaterial = Material;
			LastMaterialCBIndex = MaterialCBIndex;

			return MaterialCBIndex;
		};

	// 같은 Object가 여러 Section을 가지고 있을 경우
	// Object CB와 DepthBucket을 다시 찾거나 계산하지 않는다.
	uint32 LastObjectIndex = InvalidRenderId;
	uint32 LastObjectCBIndex = InvalidRenderId;

	uint32 LastDepthObjectIndex = InvalidRenderId;
	uint32 LastDepthBucket = 0;

	// 같은 MeshPage가 연속해서 나오는 경우 Binding 조회를 생략한다.
	uint32 LastMeshPageId = InvalidRenderId;
	FMeshPageBinding LastPageBinding{};

	// 일반 primitives 처리
	for (int32 Index = 0;
		Index < ViewLayoutData.Primitives.Num();
		++Index)
	{
		// 페이지 조회 / CB 매핑보다 먼저 제외
		if (PrimitiveVisibility[Index] == 0)
			continue;

		const FPrimitiveRenderData& Prim =
			ViewLayoutData.Primitives[Index];

		if (Prim.ObjectIndex >= Objects.Num())
			continue;

		const FRenderObjectData& ObjDatum =
			Objects[Prim.ObjectIndex];

		const FMaterial* Material = Prim.Material;

		if (!Material ||
			Material->MaterialId == InvalidRenderId)
		{
			continue;
		}

		// ---------------------------------------------------------
		// MeshPageBinding 캐시
		// ---------------------------------------------------------
		FMeshPageBinding PageBinding;

		if (Prim.Geometry.MeshPageId == LastMeshPageId)
		{
			PageBinding = LastPageBinding;
		}
		else
		{
			PageBinding =
				GResourceManager::GetInstance()
				->GetMeshPageBinding(
					Prim.Geometry.MeshPageId);

			LastMeshPageId =
				Prim.Geometry.MeshPageId;

			LastPageBinding = PageBinding;
		}

		// ---------------------------------------------------------
		// Object CB 캐시
		// ---------------------------------------------------------
		uint32 ObjectCBIdx;

		if (Prim.ObjectIndex == LastObjectIndex)
		{
			ObjectCBIdx = LastObjectCBIndex;
		}
		else
		{
			ObjectCBIdx =
				GetOrCreateObjectCBIndex(
					Prim.ObjectIndex);

			LastObjectIndex =
				Prim.ObjectIndex;

			LastObjectCBIndex =
				ObjectCBIdx;
		}

		// ---------------------------------------------------------
		// Material CB 캐시
		// ---------------------------------------------------------
		const uint32 MaterialCBIdx =
			GetOrCreateMaterialCBIndex(Material);

		// ---------------------------------------------------------
		// BlendMode
		// ---------------------------------------------------------
		const bool bIsTransparent =
			(Material->BlendMode ==
				EPrimitiveBlendMode::Additive);

		// ---------------------------------------------------------
		// DepthBucket
		// Opaque에서만 필요
		// ---------------------------------------------------------
		uint32 DepthBucket = 0;

		if (!bIsTransparent)
		{
			if (Prim.ObjectIndex == LastDepthObjectIndex)
			{
				DepthBucket = LastDepthBucket;
			}
			else
			{
				DepthBucket =
					CalculateDepthBucket(
						ObjDatum.SortCenterWS,
						ViewSnapshot.ViewMatrix);

				LastDepthBucket = DepthBucket;
				LastDepthObjectIndex = Prim.ObjectIndex;
			}
		}

		const EPipelinePass TargetPass =
			bIsTransparent
			? EPipelinePass::Additive
			: EPipelinePass::Opaque;

		// ---------------------------------------------------------
		// Pipeline
		// ---------------------------------------------------------
		const FPipelineKey Key =
			MakePipelineKey(
				Material,
				PageBinding.VertexFormat,
				TargetPass,
				ViewSnapshot.ViewMode);

		const FPipelineState* State =
			FindPipeline(Key);

		if (!State)
			continue;

		// ---------------------------------------------------------
		// Draw 생성
		// ---------------------------------------------------------
		FPreparedDraw Draw;

		Draw.Source = &Prim;
		Draw.PipelineId = State->PipelineId;
		Draw.ObjectConstantIndex = ObjectCBIdx;
		Draw.MaterialConstantIndex = MaterialCBIdx;
		Draw.DepthBucket = DepthBucket;
		Draw.MaterialId = Material->MaterialId;
		Draw.MeshPageId = Prim.Geometry.MeshPageId;
		Draw.ObjectIndex = Prim.ObjectIndex;

		// Opaque만 SortKey가 필요하다.
		if (!bIsTransparent)
		{
			Draw.SortKey =
				(static_cast<uint64>(Draw.PipelineId) << 48) |
				(static_cast<uint64>(Draw.MaterialId) << 32) |
				(static_cast<uint64>(Draw.DepthBucket) << 16) |
				(static_cast<uint64>(Draw.MeshPageId));

			OutPassDraws.OpaqueDraws.Add(Draw);
		}
		else
		{
			OutPassDraws.AdditiveDraws.Add(Draw);
		}

		// ---------------------------------------------------------
		// Outline
		// ---------------------------------------------------------
		const bool bSelected =
			(Prim.Flags & Primitive_Selected) != 0;

		const bool bAllowOutline =
			(Prim.Flags & Primitive_AllowOutline) != 0;

		if (bSelected && bAllowOutline)
		{
			const FPipelineKey OutlineKey =
				MakePipelineKey(
					Material,
					PageBinding.VertexFormat,
					EPipelinePass::Outline,
					ViewSnapshot.ViewMode);

			const FPipelineState* OutlineState =
				FindPipeline(OutlineKey);

			if (OutlineState)
			{
				FPreparedDraw OutlineDraw = Draw;
				OutlineDraw.PipelineId =
					OutlineState->PipelineId;

				OutPassDraws.OutlineDraws.Add(
					OutlineDraw);
			}
		}
	}

	// ---------------------------------------------------------
	// 기즈모 처리
	// ---------------------------------------------------------
	for (const FPrimitiveRenderData& GizmoPrim :
		ViewLayoutData.Gizmos)
	{
		if (GizmoPrim.ObjectIndex >= Objects.Num())
			continue;

		const FRenderObjectData& ObjDatum =
			Objects[GizmoPrim.ObjectIndex];

		const FMaterial* Material =
			GizmoPrim.Material;

		if (!Material ||
			Material->MaterialId == InvalidRenderId)
		{
			continue;
		}

		// ---------------------------------------------------------
		// MeshPageBinding 캐시
		// ---------------------------------------------------------
		FMeshPageBinding PageBinding;

		if (GizmoPrim.Geometry.MeshPageId == LastMeshPageId)
		{
			PageBinding = LastPageBinding;
		}
		else
		{
			PageBinding =
				GResourceManager::GetInstance()
				->GetMeshPageBinding(
					GizmoPrim.Geometry.MeshPageId);

			LastMeshPageId =
				GizmoPrim.Geometry.MeshPageId;

			LastPageBinding = PageBinding;
		}

		// ---------------------------------------------------------
		// Object CB 캐시
		// ---------------------------------------------------------
		uint32 ObjectCBIdx;

		if (GizmoPrim.ObjectIndex == LastObjectIndex)
		{
			ObjectCBIdx = LastObjectCBIndex;
		}
		else
		{
			ObjectCBIdx =
				GetOrCreateObjectCBIndex(
					GizmoPrim.ObjectIndex);

			LastObjectIndex =
				GizmoPrim.ObjectIndex;

			LastObjectCBIndex =
				ObjectCBIdx;
		}

		// Gizmo에서도 Material 캐시는 재사용
		const uint32 MaterialCBIdx =
			GetOrCreateMaterialCBIndex(Material);

		EPipelinePass GizmoPass;

		if (GizmoPrim.Topology ==
			D3D10_PRIMITIVE_TOPOLOGY_LINELIST)
		{
			GizmoPass = EPipelinePass::Line;
		}
		else
		{
			GizmoPass = EPipelinePass::Gizmo;
		}

		const FPipelineKey Key =
			MakePipelineKey(
				Material,
				PageBinding.VertexFormat,
				GizmoPass,
				ViewSnapshot.ViewMode);

		const FPipelineState* State =
			FindPipeline(Key);

		if (!State)
			continue;

		FPreparedDraw Draw;

		Draw.Source = &GizmoPrim;
		Draw.PipelineId = State->PipelineId;
		Draw.ObjectConstantIndex = ObjectCBIdx;
		Draw.MaterialConstantIndex = MaterialCBIdx;
		Draw.DepthBucket = 0;
		Draw.MaterialId = Material->MaterialId;
		Draw.MeshPageId = GizmoPrim.Geometry.MeshPageId;
		Draw.ObjectIndex = GizmoPrim.ObjectIndex;

		// Gizmo는 현재 정렬하지 않으므로 SortKey 계산 안 함.
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

uint32 FPassDrawBuilder::CalculateDepthBucket(
	const FVector& SortCenterWS, const FFastMatrix& ViewMatrix, float NearZ, float FarZ) const
{
	// 정렬에는 Z만 필요하므로 X, Y, W 성분의 변환을 생략합니다.
	const float ViewZ =
		SortCenterWS.X * ViewMatrix.M[0][2] +
		SortCenterWS.Y * ViewMatrix.M[1][2] +
		SortCenterWS.Z * ViewMatrix.M[2][2] +
		ViewMatrix.M[3][2];

	// 기존 깊이 범위와 버킷 생성 규칙을 유지합니다.
	const float NormalizedDepth =
		std::clamp((ViewZ - NearZ) / (FarZ - NearZ), 0.0f, 1.0f);
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
