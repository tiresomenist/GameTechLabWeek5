#include "pch.h"
#include "ObjectPicker.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Matrix.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Log.h"
#include "Engine/Component/CameraComponent.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Editor/Editor.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Component/Light/SpotLightComponent.h"
#include "Editor/Util/ScopeCycleCounter.h"

// Todo: BVH
#include "Engine/Scene/SceneBVH.h"

#include <cstddef>
#include <vector>

namespace
{
    // 보통의 BVH 깊이는 호출 스택에서 처리하고, 더 깊은 트리만 동적 배열을 사용한다.
    template <typename T, std::size_t InlineCapacity = 128>
    class TInlineBVHStack
    {
    public:
        void Add(const T& Entry)
        {
            if (Overflow.empty() && InlineCount < InlineCapacity)
            {
                Inline[InlineCount++] = Entry;
            }
            else
            {
                Overflow.push_back(Entry);
            }
        }

        T Pop()
        {
            if (!Overflow.empty())
            {
                T Entry = Overflow.back();
                Overflow.pop_back();
                return Entry;
            }

            return Inline[--InlineCount];
        }

        bool IsEmpty() const
        {
            return InlineCount == 0 && Overflow.empty();
        }

    private:
        T Inline[InlineCapacity];
        std::size_t InlineCount = 0;
        std::vector<T> Overflow;
    };
}

struct FRayAABBCache
{
	explicit FRayAABBCache(const FRay& Ray)
		: InverseDirection(
			Ray.Direction.X != 0.0f ? 1.0f / Ray.Direction.X : 0.0f,
			Ray.Direction.Y != 0.0f ? 1.0f / Ray.Direction.Y : 0.0f,
			Ray.Direction.Z != 0.0f ? 1.0f / Ray.Direction.Z : 0.0f)
	{
		bDivideX = !std::isfinite(InverseDirection.X);
		bDivideY = !std::isfinite(InverseDirection.Y);
		bDivideZ = !std::isfinite(InverseDirection.Z);
		bFastPath = Ray.Direction.X != 0.0f && Ray.Direction.Y != 0.0f && Ray.Direction.Z != 0.0f
			&& !bDivideX && !bDivideY && !bDivideZ;
	}

	FVector InverseDirection;
	bool bDivideX = false;
	bool bDivideY = false;
	bool bDivideZ = false;
	bool bFastPath = false;
};

FObjectPicker::FObjectPicker(FEditor* InEditor)
	: Editor{ InEditor }
{
}

bool FObjectPicker::MakeWorldRay(FRay& OutRay, D3D11_VIEWPORT InViewport) {
	UCameraComponent* Camera = Editor->GetEditorCamera();

	auto& Input = *GInputManager::GetInstance();

	float PixelX = Input.GetLeftCursorPixelX();
	float PixelY = Input.GetLeftCursorPixelY();

	float NDCX = 2.0f * (PixelX - InViewport.TopLeftX) / InViewport.Width - 1.0f;
	float NDCY = 1.0f - 2.0f * (PixelY - InViewport.TopLeftY) / InViewport.Height;

	FVector4 Near(NDCX, NDCY, 0.0f, 1.0f);
	FVector4 Far(NDCX, NDCY, 1.0f, 1.0f);

	FFastMatrix VPInverse;
	if (!(Camera->GetViewMatrix() * Camera->GetProjectionMatrix()).TryInverse(VPInverse)) {
		return false;
	}

	FVector4 NearWorld = Near * VPInverse;	//World에서의 Ray 시작점
	FVector4 FarWorld = Far * VPInverse; //World에서의 Ray 끝점

	if (std::fabs(NearWorld.W) < 1.0e-6f || std::fabs(FarWorld.W) < 1.0e-6f) return false;

	NearWorld = FVector4(FVector(NearWorld) / NearWorld.W, 1.0f);
	FarWorld = FVector4(FVector(FarWorld) / FarWorld.W, 1.0f);

	FVector4 Direction = FVector4((FVector(FarWorld) - FVector(NearWorld)).GetNormalized(), 0.0f);	//Ray 방향
	
	OutRay.Origin = FVector(NearWorld);
	OutRay.Direction = FVector(Direction);
	return true;
}

bool FObjectPicker::RayTriangleIntersect(const FRay& Ray, const FVector& A, const FVector& B, const FVector& C, float& OutDistance) {
	const float Edge1X = B.X - A.X;
	const float Edge1Y = B.Y - A.Y;
	const float Edge1Z = B.Z - A.Z;
	const float Edge2X = C.X - A.X;
	const float Edge2Y = C.Y - A.Y;
	const float Edge2Z = C.Z - A.Z;

	const float PX = Ray.Direction.Y * Edge2Z - Ray.Direction.Z * Edge2Y;
	const float PY = Ray.Direction.Z * Edge2X - Ray.Direction.X * Edge2Z;
	const float PZ = Ray.Direction.X * Edge2Y - Ray.Direction.Y * Edge2X;
	const float Determinant = Edge1X * PX + Edge1Y * PY + Edge1Z * PZ;

	if (std::fabs(Determinant) < 1.0e-6f)
		return false; // 평행 또는 퇴화 삼각형

	const float InverseDeterminant = 1.0f / Determinant;

	const float TX = Ray.Origin.X - A.X;
	const float TY = Ray.Origin.Y - A.Y;
	const float TZ = Ray.Origin.Z - A.Z;
	const float U = (TX * PX + TY * PY + TZ * PZ) * InverseDeterminant;
	if (U < 0.0f || U > 1.0f)
		return false;

	const float QX = TY * Edge1Z - TZ * Edge1Y;
	const float QY = TZ * Edge1X - TX * Edge1Z;
	const float QZ = TX * Edge1Y - TY * Edge1X;
	const float V = (Ray.Direction.X * QX + Ray.Direction.Y * QY + Ray.Direction.Z * QZ) * InverseDeterminant;
	if (V < 0.0f || U + V > 1.0f)
		return false;

	OutDistance = (Edge2X * QX + Edge2Y * QY + Edge2Z * QZ) * InverseDeterminant;
	return OutDistance > 1.0e-6f;
}

bool FObjectPicker::RayAABBIntersect(const FRay& Ray,const FVector& BoundsMin,const FVector& BoundsMax,float MaxDistance,float& OutDistance)
{
	float Enter = 0.0f;
	float Exit = MaxDistance;

	auto TestAxis = [&](float Origin, float Direction, float Min, float Max) -> bool
		{
			// 이 축으로 움직이지 않으면 시작점이 범위 안에 있어야 함.
			if (Direction == 0.0f)
				return Origin >= Min && Origin <= Max;

			float T0 = (Min - Origin) / Direction;
			float T1 = (Max - Origin) / Direction;

			if (T0 > T1)
				std::swap(T0, T1);

			Enter = (std::max)(Enter, T0);
			Exit = (std::min)(Exit, T1);

			// 같을 때도 허용해야 두께가 0인 Plane·Triangle을 검사할 수 있음.
			return Enter <= Exit;
		};

	if (!TestAxis(Ray.Origin.X, Ray.Direction.X, BoundsMin.X, BoundsMax.X))
		return false;

	if (!TestAxis(Ray.Origin.Y, Ray.Direction.Y, BoundsMin.Y, BoundsMax.Y))
		return false;

	if (!TestAxis(Ray.Origin.Z, Ray.Direction.Z, BoundsMin.Z, BoundsMax.Z))
		return false;

	OutDistance = Enter;
	return true;
}

// Todo: 인자 수 줄이기
// Slab AABB Intersect
bool FObjectPicker::HasRayAABBIntersected(const FRay& Ray, const FRayAABBCache& Cache, const FVector& BoundsMin, const FVector& BoundsMax, float MaxDistance, float& OutIntersectedDistance)
{
	float Enter = 0.0f;
	float Exit = MaxDistance;
	if (Cache.bFastPath)
	{
		float Near = (BoundsMin.X - Ray.Origin.X) * Cache.InverseDirection.X;
		float Far = (BoundsMax.X - Ray.Origin.X) * Cache.InverseDirection.X;
		if (Near > Far) { const float Temp = Near; Near = Far; Far = Temp; }
		if (Near > Enter) Enter = Near;
		if (Far < Exit) Exit = Far;
		if (Enter > Exit) return false;

		Near = (BoundsMin.Y - Ray.Origin.Y) * Cache.InverseDirection.Y;
		Far = (BoundsMax.Y - Ray.Origin.Y) * Cache.InverseDirection.Y;
		if (Near > Far) { const float Temp = Near; Near = Far; Far = Temp; }
		if (Near > Enter) Enter = Near;
		if (Far < Exit) Exit = Far;
		if (Enter > Exit) return false;

		Near = (BoundsMin.Z - Ray.Origin.Z) * Cache.InverseDirection.Z;
		Far = (BoundsMax.Z - Ray.Origin.Z) * Cache.InverseDirection.Z;
		if (Near > Far) { const float Temp = Near; Near = Far; Far = Temp; }
		if (Near > Enter) Enter = Near;
		if (Far < Exit) Exit = Far;
		if (Enter > Exit) return false;

		OutIntersectedDistance = Enter;
		return true;
	}

	if (Ray.Direction.X == 0.0f)
	{
		if (Ray.Origin.X < BoundsMin.X || Ray.Origin.X > BoundsMax.X) return false;
	}
	else
	{
		float Near = Cache.bDivideX
			? (BoundsMin.X - Ray.Origin.X) / Ray.Direction.X
			: (BoundsMin.X - Ray.Origin.X) * Cache.InverseDirection.X;
		float Far = Cache.bDivideX
			? (BoundsMax.X - Ray.Origin.X) / Ray.Direction.X
			: (BoundsMax.X - Ray.Origin.X) * Cache.InverseDirection.X;
		if (Near > Far) { const float Temp = Near; Near = Far; Far = Temp; }
		if (Near > Enter) Enter = Near;
		if (Far < Exit) Exit = Far;
		if (Enter > Exit) return false;
	}

	if (Ray.Direction.Y == 0.0f)
	{
		if (Ray.Origin.Y < BoundsMin.Y || Ray.Origin.Y > BoundsMax.Y) return false;
	}
	else
	{
		float Near = Cache.bDivideY
			? (BoundsMin.Y - Ray.Origin.Y) / Ray.Direction.Y
			: (BoundsMin.Y - Ray.Origin.Y) * Cache.InverseDirection.Y;
		float Far = Cache.bDivideY
			? (BoundsMax.Y - Ray.Origin.Y) / Ray.Direction.Y
			: (BoundsMax.Y - Ray.Origin.Y) * Cache.InverseDirection.Y;
		if (Near > Far) { const float Temp = Near; Near = Far; Far = Temp; }
		if (Near > Enter) Enter = Near;
		if (Far < Exit) Exit = Far;
		if (Enter > Exit) return false;
	}

	if (Ray.Direction.Z == 0.0f)
	{
		if (Ray.Origin.Z < BoundsMin.Z || Ray.Origin.Z > BoundsMax.Z) return false;
	}
	else
	{
		float Near = Cache.bDivideZ
			? (BoundsMin.Z - Ray.Origin.Z) / Ray.Direction.Z
			: (BoundsMin.Z - Ray.Origin.Z) * Cache.InverseDirection.Z;
		float Far = Cache.bDivideZ
			? (BoundsMax.Z - Ray.Origin.Z) / Ray.Direction.Z
			: (BoundsMax.Z - Ray.Origin.Z) * Cache.InverseDirection.Z;
		if (Near > Far) { const float Temp = Near; Near = Far; Far = Temp; }
		if (Near > Enter) Enter = Near;
		if (Far < Exit) Exit = Far;
		if (Enter > Exit) return false;
	}

	OutIntersectedDistance = Enter;
	return true;
}

// 피킹 시간 계산 로직 추가
USceneComponent* FObjectPicker::Pick()
{
	if (!Editor) { return nullptr; }

	UScene* Scene = Editor->GetCurrentScene();
	const UCameraComponent* Camera = Editor->GetEditorCamera();

	D3D11_VIEWPORT CurrViewport = Editor->GetViewports()[Editor->GetCurrentEditViewportIndex()].GetRenderView().Viewport;

	if (!Scene || !Camera) { return nullptr; }

	// 숨겨진 프리미티브와 아이콘을 선택하지 않도록 처리함
	if (!Editor->IsShowingPrimitives()) { return nullptr; }

	FRay Ray;
	if (!MakeWorldRay(Ray, CurrViewport)) { return nullptr; }

	// 피킹 횟수 계산 시작
	FScopeCycleCounter PickCounter;
	TotalPickCount++;

	// 프리미티브와 광원을 같은 선택 결과로 취급함
	USceneComponent* SelectedObject = nullptr;
	float ClosestDistance = 100000.0f;
	
	//PickPrimitives(Scene, Ray, ClosestDistance, SelectedObject);
	//PickIcon(Scene,Camera, Ray, ClosestDistance, SelectedObject);
	
	// Pick primitive
	{
		const FRayAABBCache WorldCache(Ray);
		PickSceneBVHNodeIterative(Scene->GetBVHRoot(), Ray, WorldCache, ClosestDistance, SelectedObject);

		// Todo: 함수 포인터 사용하지 말아보자
		// 이번 단계에서 BVH에 없는 텍스트·플립북
		Scene->ForEachNonStaticMesh([&](UPrimitiveComponent* Primitive)
			{
				TestPrimitive(Primitive, Ray, ClosestDistance, SelectedObject);
			});
	}

	LastPickTimeMs = PickCounter.Finish();
	TotalPickTimeMs += LastPickTimeMs;

	// 임시로 피킹 결과값을 로그로 찍는다. 추후에 스탯창처럼 띄우는게 나을듯 함.
	UE_LOG("[Picking] Count={} Last={:.3f} ms Total={:.3f} ms Avg={:.3f} ms",
		TotalPickCount, LastPickTimeMs, TotalPickTimeMs,
		TotalPickTimeMs / static_cast<double>(TotalPickCount));

	return SelectedObject;
}

/*
void FObjectPicker::PickPrimitives(UScene* Scene, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
{
	// 정적 메시
	const FRayAABBCache WorldCache(Ray);
	PickSceneBVHNodeRecursive(Scene->GetBVHRoot(), Ray, WorldCache, ClosestDistance, SelectedObject);

	// 이번 단계에서 BVH에 없는 텍스트·플립북
	Scene->ForEachNonStaticMesh([&](UPrimitiveComponent* Primitive)
		{
			TestPrimitive(Primitive, Ray, ClosestDistance, SelectedObject);
		});
}
*/

/*
void FObjectPicker::PickBVHNodeRecursive(const FSceneBVHNode* Node, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
{
	if (!Node) return;

	float Distance = 0.0f;
	if (!RayAABBIntersect(Ray, Node->WorldMin, Node->WorldMax, ClosestDistance, Distance))
	{
		return;
	}

	if (Node->ComponentOrNull)
	{
		TestPrimitive(Node->ComponentOrNull, Ray, ClosestDistance, SelectedObject);
		return;
	}

	PickBVHNodeRecursive(Node->LeftChild, Ray, ClosestDistance, SelectedObject);
	PickBVHNodeRecursive(Node->RightChild, Ray, ClosestDistance, SelectedObject);
}
*/

/*
void FObjectPicker::TestPrimitive(UPrimitiveComponent* Primitive, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
{
	if (!Primitive || !Primitive->IsVisible())
		return;

	FVector BoundsMin;
	FVector BoundsMax;
	if (!Primitive->GetLocalBounds(BoundsMin, BoundsMax))
		return;

	FMatrix InverseWorld;
	const FMatrix& RenderWorld =
		Primitive->GetRenderWorldMatrix(Editor->GetEditorCamera());

	if (!RenderWorld.TryInverse(InverseWorld))
		return;

	FRay LocalRay;
	LocalRay.Origin =
		FVector(FVector4(Ray.Origin, 1.0f) * InverseWorld);
	LocalRay.Direction =
		FVector(FVector4(Ray.Direction, 0.0f) * InverseWorld);

	float AABBDistance = 0.0f;
	if (!RayAABBIntersect(
		LocalRay, BoundsMin, BoundsMax,
		ClosestDistance, AABBDistance))
	{
		return;
	}

	if (Primitive->IsAABBOnlyPickable())
	{
		ClosestDistance = AABBDistance;
		SelectedObject = Primitive;
		return;
	}

	FMeshResource* Mesh = Primitive->GetMeshResource();
	if (!Mesh)
		return;

	const size_t Count = Mesh->GetIndices().Num();
	const size_t VertexCount = Mesh->GetPositions().Num();

	if (Count != Mesh->GetIndexCount() || Count % 3 != 0)
		return;

	for (size_t Index = 0; Index < Count; Index += 3)
	{
		const uint32 I0 = Mesh->GetIndices()[Index];
		const uint32 I1 = Mesh->GetIndices()[Index + 1];
		const uint32 I2 = Mesh->GetIndices()[Index + 2];

		if (I0 >= VertexCount ||
			I1 >= VertexCount ||
			I2 >= VertexCount)
		{
			continue;
		}

		const FVector& A = Mesh->GetPositions()[I0];
		const FVector& B = Mesh->GetPositions()[I1];
		const FVector& C = Mesh->GetPositions()[I2];

		float HitDistance = 0.0f;
		if (RayTriangleIntersect(
			LocalRay, A, B, C, HitDistance) &&
			HitDistance < ClosestDistance)
		{
			ClosestDistance = HitDistance;
			SelectedObject = Primitive;
		}
	}
}
*/

// Todo: BVH
void FObjectPicker::TestPrimitive(UPrimitiveComponent* Primitive, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
{
	// 보이지 않는 프리미티브는 선택하지 않는다.
	if (Primitive == nullptr || !Primitive->IsVisible())
	{
		return;
	}

	//Todo: Picking cache
	// 월드 레이를 프리미티브의 로컬 좌표로 변환한다.
	FMatrix InverseWorld;
	if (!Primitive->TryGetInverseRenderWorldMatrix(Editor->GetEditorCamera(), InverseWorld))
	{
		return;
	}

	FRay LocalRay;
	LocalRay.Origin = FVector(FVector4(Ray.Origin, 1.0f) * InverseWorld);
	LocalRay.Direction = FVector(FVector4(Ray.Direction, 0.0f) * InverseWorld);

	// 텍스트·플립북처럼 AABB만으로 선택하는 프리미티브를 처리한다.
	if (Primitive->IsAABBOnlyPickable())
	{
		FVector BoundsMin;
		FVector BoundsMax;
		if (!Primitive->GetLocalBounds(BoundsMin, BoundsMax))
		{
			return;
		}

		float AABBDistance = 0.0f;
		if (RayAABBIntersect(LocalRay, BoundsMin, BoundsMax, ClosestDistance, AABBDistance) == false)
		{
			return;
		}

		ClosestDistance = AABBDistance;
		SelectedObject = Primitive;

		return;
	}

	FMeshResource* Mesh = Primitive->GetMeshResource();
	if (Mesh == nullptr)
	{
		return;
	}

	const FMeshBVH& MeshBVH = Mesh->GetTriangleBVH();
	const FRayAABBCache LocalCache(LocalRay);

	// 메시 BVH를 탐색해 가장 가까운 삼각형 교차점을 찾는다.
	if (PickMeshBVHNodeIterative(MeshBVH.GetRoot(), MeshBVH, *Mesh, LocalRay, LocalCache, ClosestDistance))
	{
		SelectedObject = Primitive;
	}
}

// Todo: BVH
// 프리미티브 피킹 부분, 추가적으로 최적화해야함.
/*
void FObjectPicker::PickPrimitives(UScene* Scene, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
{
	Scene->ForEachPrimitive([&](UPrimitiveComponent* Primitive)
		{
			// Visible 끈 오브젝트는 피킹되지 않음
			if (!Primitive) return;
			if (!Primitive->IsVisible()) return;
			FVector BoundsMin;
			FVector BoundsMax;
			if (!Primitive->GetLocalBounds(BoundsMin, BoundsMax)) return;

			FMatrix InverseWorld;
			const FMatrix& RenderWorldMatrix = Primitive->GetRenderWorldMatrix(Editor->GetEditorCamera());
			if (!RenderWorldMatrix.TryInverse(InverseWorld)) return;
			FRay LocalRay;
			LocalRay.Origin = FVector(FVector4(Ray.Origin, 1.0f) * InverseWorld);
			LocalRay.Direction = FVector(FVector4(Ray.Direction, 0.0f) * InverseWorld);


			//AABB로 후보 선택.
			float AABBDistance = 0.0f;
			if (!RayAABBIntersect(LocalRay, BoundsMin, BoundsMax, ClosestDistance, AABBDistance)) return;
			if (Primitive->IsAABBOnlyPickable())
			{
				ClosestDistance = AABBDistance;
				SelectedObject = Primitive;
				return;
			}

			//뮐러-트럼보어로 실제 피킹 처리
			FMeshResource* Mesh = Primitive->GetMeshResource();
			if (!Mesh) return;
			const size_t Count = Mesh->GetIndices().Num();
			const size_t VertexCount = Mesh->GetPositions().Num();
			if (Count != Mesh->GetIndexCount() || Count % 3 != 0) return;
			for (size_t Index = 0; Index < Count; Index += 3)
			{
				const uint32 I0 = Mesh->GetIndices()[Index];
				const uint32 I1 = Mesh->GetIndices()[Index + 1];
				const uint32 I2 = Mesh->GetIndices()[Index + 2];
				if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount) continue;
				const FVector& A = Mesh->GetPositions()[I0];
				const FVector& B = Mesh->GetPositions()[I1];
				const FVector& C = Mesh->GetPositions()[I2];
				float T;
				if (RayTriangleIntersect(LocalRay, A, B, C, T))
				{
					if (T < ClosestDistance)
					{
						ClosestDistance = T;
						SelectedObject = Primitive;
					}
				}
			}
		});
}

*/

// 현재는 Spotlight Icon만 검사중. 장기적으로 구조를 바꿔서 모든 메쉬없는 아이콘에 대해 피킹되도록 해야함
// UBillboardComponent를 하나 파서, 해당 아이콘이 광원/카메라 등의 메쉬없는 컴포넌트에 연결되도록 하는 형태로 바꾸면 될듯함.
//void FObjectPicker::PickIcon(UScene* Scene, const UCameraComponent* Camera, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
//{
//	Scene->ForEachActor([&](AActor* Actor)
//		{
//			for (UActorComponent* Component : Actor->GetComponents())
//			{
//				if (!Component->IsA(USpotLightComponent::GetClass())) continue;
//				auto* SpotLight = static_cast<USpotLightComponent*>(Component);
//				if (!SpotLight->IsVisible()) continue;
//
//				// 실제 렌더링과 동일한 행렬 및 로컬 범위를 조회함
//				const FPrimitiveRenderData IconData = SpotLight->BuildIconRenderData(Camera, false);
//				if (!IconData.VertexBuffer || !IconData.IndexBuffer || !IconData.Material.SRV ||
//					!IconData.WorldMatrix || IconData.IndexCount == 0)
//				{
//					continue;
//				}
//				// 크기가 0인 경우 등 역변환이 불가능한 아이콘을 제외함
//				FMatrix InverseWorld;
//				if (!IconData.WorldMatrix->TryInverse(InverseWorld)) continue;
//
//				// 월드 광선을 아이콘의 로컬 공간으로 변환함
//				FRay LocalRay;
//				LocalRay.Origin = FVector(FVector4(Ray.Origin, 1.0f) * InverseWorld);
//
//				// 거리 비교 기준을 유지하기 위해 정규화하지 않음
//				LocalRay.Direction = FVector(FVector4(Ray.Direction, 0.0f) * InverseWorld);
//				// 로컬 XY 평면의 아이콘 사각형과 교차 검사함
//				float HitDistance = 0.0f;
//				if (!RayAABBIntersect(LocalRay, IconData.Min, IconData.Max, ClosestDistance, HitDistance))
//				{
//					continue;
//				}
//
//				// 기존 프리미티브 결과보다 가까운 아이콘만 선택함
//				if (HitDistance > EPSILON && HitDistance < ClosestDistance)
//				{
//					ClosestDistance = HitDistance;
//					SelectedObject = SpotLight;
//				}
//			}
//		});
//}

/*
// Todo: BVH Mesh
bool FObjectPicker::PickMeshBVHNodeRecursive(const FMeshBVHNode* Node, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, float& ClosestDistance)
{
	if (Node == nullptr)
	{
		return false;
	}

	float EnterDistance = 0.0f;

	// 레이가 노드 AABB를 빗나가면 그 아래 삼각형을 모두 건너뛴다.
	if (RayAABBIntersect(LocalRay, Node->LocalMin, Node->LocalMax, ClosestDistance, EnterDistance) == false)
	{
		return false;
	}

	// 리프라면 이 노드에 배정된 삼각형만 정확히 검사한다.
	if (Node->TrianglesCount > 0)
	{
		const TArray<uint32>& TriangleOrders = BVH.GetTriangleOrders();
		const TArray<uint32>& Indices = Mesh.GetIndices();
		const TArray<FVector>& Positions = Mesh.GetPositions();

		bool bHit = false;
		const uint32 End = Node->FirstTriangleOffset + Node->TrianglesCount;

		for (uint32 i = Node->FirstTriangleOffset; i < End; ++i)
		{
			const uint32 TriangleIndex = TriangleOrders[i];
			const uint32 Base = TriangleIndex * 3;

			float HitDistance = 0.0f;
			if (RayTriangleIntersect(LocalRay, Positions[Indices[Base]], Positions[Indices[Base + 1]], Positions[Indices[Base + 2]], HitDistance) && HitDistance < ClosestDistance)
			{
				ClosestDistance = HitDistance;
				bHit = true;
			}
		}

		return bHit;
	}

	// 내부 노드라면 왼쪽과 오른쪽 자식을 모두 탐색한다.
	const bool bLeftHit = PickMeshBVHNodeRecursive(Node->LeftChild, BVH, Mesh, LocalRay, ClosestDistance);
	const bool bRightHit = PickMeshBVHNodeRecursive(Node->RightChild, BVH, Mesh, LocalRay, ClosestDistance);

	return bLeftHit || bRightHit;
}
*/

void FObjectPicker::PickSceneBVHNodeIterative(const FSceneBVHNode* Root, const FRay& Ray, const FRayAABBCache& Cache, float& ClosestCandidateDistance, USceneComponent*& SelectedObject)
{
	if (Root == nullptr)
	{
		return;
	}

	float RootDistance = 0.0f;
	if (HasRayAABBIntersected(Ray, Cache, Root->WorldMin, Root->WorldMax, ClosestCandidateDistance, RootDistance) == false)
	{
		return;
	}

	struct FVisit
	{
		const FSceneBVHNode* Node;
		float EntryDistance;
	};

	TInlineBVHStack<FVisit> Stack;
	FVisit Current{ Root, RootDistance };

	while (true)
	{
		if (Current.EntryDistance <= ClosestCandidateDistance)
		{
			const FSceneBVHNode* NodeOrNull = Current.Node;

			// 리프에 도달하면 해당 메시를 정확히 검사한다.
			if (NodeOrNull->ComponentOrNull != nullptr)
			{
				TestPrimitive(NodeOrNull->ComponentOrNull, Ray, ClosestCandidateDistance, SelectedObject);
			}
			else
			{
				const FSceneBVHNode* Left = NodeOrNull->LeftChild;
				const FSceneBVHNode* Right = NodeOrNull->RightChild;
				float LeftDistance = 0.0f;
				float RightDistance = 0.0f;
				const bool bLeftHit = HasRayAABBIntersected(Ray, Cache, Left->WorldMin, Left->WorldMax, ClosestCandidateDistance, LeftDistance);
				const bool bRightHit = HasRayAABBIntersected(Ray, Cache, Right->WorldMin, Right->WorldMax, ClosestCandidateDistance, RightDistance);

				if (bLeftHit && bRightHit)
				{
					if (LeftDistance <= RightDistance)
					{
						Stack.Add({ Right, RightDistance });
						Current = { Left, LeftDistance };
					}
					else
					{
						Stack.Add({ Left, LeftDistance });
						Current = { Right, RightDistance };
					}
					continue;
				}
				if (bLeftHit)
				{
					Current = { Left, LeftDistance };
					continue;
				}
				if (bRightHit)
				{
					Current = { Right, RightDistance };
					continue;
				}
			}
		}

		if (Stack.IsEmpty())
		{
			break;
		}
		Current = Stack.Pop();
	}
}

bool FObjectPicker::PickMeshBVHNodeIterative(const FMeshBVHNode* Root, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, const FRayAABBCache& Cache, float& ClosestDistance)
{
	if (Root == nullptr)
	{
		return false;
	}

	float RootDistance = 0.0f;
	if (HasRayAABBIntersected(LocalRay, Cache, Root->LocalMin, Root->LocalMax, ClosestDistance, RootDistance) == false)
	{
		return false;
	}

	struct FVisit
	{
		const FMeshBVHNode* Node;
		float EntryDistance;
	};

	// 중앙 분할 메시 BVH의 깊이는 uint32 삼각형 개수 기준 32보다 작다.
	FVisit Stack[32];
	uint32 StackCount = 0;
	FVisit Current{ Root, RootDistance };
	bool bHit = false;
	const uint32* TriangleOrders = BVH.GetTriangleOrders().GetData();
	const uint32* Indices = Mesh.GetIndices().GetData();
	const FVector* Positions = Mesh.GetPositions().GetData();

	while (true)
	{
		if (Current.EntryDistance <= ClosestDistance)
		{
			const FMeshBVHNode* Node = Current.Node;

			// 리프에 배정된 삼각형만 정확히 검사한다.
			if (Node->TrianglesCount > 0)
			{
				const uint32 End = Node->FirstTriangleOffset + Node->TrianglesCount;

				for (uint32 Index = Node->FirstTriangleOffset; Index < End; ++Index)
				{
					const uint32 TriangleIndex = TriangleOrders[Index];
					const uint32 Base = TriangleIndex * 3;

					float HitDistance = 0.0f;

					if (RayTriangleIntersect(LocalRay, Positions[Indices[Base]], Positions[Indices[Base + 1]], Positions[Indices[Base + 2]], HitDistance) && HitDistance < ClosestDistance)
					{
						ClosestDistance = HitDistance;
						bHit = true;
					}
				}

			}
			else
			{
				const FMeshBVHNode* Left = Node->LeftChild;
				const FMeshBVHNode* Right = Node->RightChild;
				float LeftDistance = 0.0f;
				float RightDistance = 0.0f;
				const bool bLeftHit = HasRayAABBIntersected(LocalRay, Cache, Left->LocalMin, Left->LocalMax, ClosestDistance, LeftDistance);
				const bool bRightHit = HasRayAABBIntersected(LocalRay, Cache, Right->LocalMin, Right->LocalMax, ClosestDistance, RightDistance);

				if (bLeftHit && bRightHit)
				{
					if (LeftDistance <= RightDistance)
					{
						Stack[StackCount++] = { Right, RightDistance };
						Current = { Left, LeftDistance };
					}
					else
					{
						Stack[StackCount++] = { Left, LeftDistance };
						Current = { Right, RightDistance };
					}
					continue;
				}
				if (bLeftHit)
				{
					Current = { Left, LeftDistance };
					continue;
				}
				if (bRightHit)
				{
					Current = { Right, RightDistance };
					continue;
				}
			}
		}

		if (StackCount == 0)
		{
			break;
		}
		Current = Stack[--StackCount];
	}

	return bHit;
}
