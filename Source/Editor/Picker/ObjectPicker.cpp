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
	FMatrix Projection = Camera->GetProjectionMatrix();
	FMatrix View = Camera->GetViewMatrix();

	FMatrix VPInverse;
	if (!(View * Projection).TryInverse(VPInverse)) {
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

bool FObjectPicker::RayTriangleIntersect(const FRay& Ray,FVector A, FVector B, FVector C ,float& OutDistance ) {
	const FVector Edge1 = B - A;
	const FVector Edge2 = C - A;

	const FVector P = Ray.Direction.Cross(Edge2);
	const float Determinant = Edge1.Dot(P);

	if (std::fabs(Determinant) < 1.0e-6f)
		return false; // 평행 또는 퇴화 삼각형

	const float InverseDeterminant = 1.0f / Determinant;

	const FVector T = Ray.Origin - A;
	const float U = T.Dot(P) * InverseDeterminant;
	if (U < 0.0f || U > 1.0f)
		return false;

	const FVector Q = T.Cross(Edge1);
	const float V = Ray.Direction.Dot(Q) * InverseDeterminant;
	if (V < 0.0f || U + V > 1.0f)
		return false;

	OutDistance = Edge2.Dot(Q) * InverseDeterminant;
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
	
	PickPrimitives(Scene, Ray, ClosestDistance, SelectedObject);
	//PickIcon(Scene,Camera, Ray, ClosestDistance, SelectedObject);
	
	LastPickTimeMs = PickCounter.Finish();
	TotalPickTimeMs += LastPickTimeMs;

	// 임시로 피킹 결과값을 로그로 찍는다. 추후에 스탯창처럼 띄우는게 나을듯 함.
	UE_LOG("[Picking] Count={} Last={:.3f} ms Total={:.3f} ms Avg={:.3f} ms",
		TotalPickCount, LastPickTimeMs, TotalPickTimeMs,
		TotalPickTimeMs / static_cast<double>(TotalPickCount));

	return SelectedObject;
}

void FObjectPicker::PickPrimitives(UScene* Scene, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject)
{
	// 정적 메시
	PickBVHNodeRecursive(Scene->GetBVHRoot(), Ray, ClosestDistance, SelectedObject);

	// 이번 단계에서 BVH에 없는 텍스트·플립북
	Scene->ForEachPrimitive([&](UPrimitiveComponent* Primitive)
		{
			if (Primitive->IsA(UStaticMeshComponent::GetClass()))
				return;

			TestPrimitive(Primitive, Ray, ClosestDistance, SelectedObject);
		});
}

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

	FVector BoundsMin;
	FVector BoundsMax;

	if (!Primitive->GetLocalBounds(BoundsMin, BoundsMax))
	{
		return;
	}

	// 월드 레이를 메시의 로컬 좌표로 변환한다.
	const FMatrix& RenderWorld = Primitive->GetRenderWorldMatrix(Editor->GetEditorCamera());
	FMatrix InverseWorld;

	if (!RenderWorld.TryInverse(InverseWorld))
	{
		return;
	}

	FRay LocalRay;
	LocalRay.Origin = FVector(FVector4(Ray.Origin, 1.0f) * InverseWorld);
	LocalRay.Direction = FVector(FVector4(Ray.Direction, 0.0f) * InverseWorld);

	// 프리미티브 전체 AABB를 빗나가면 내부 삼각형을 검사하지 않는다.
	float AABBDistance = 0.0f;
	if (!RayAABBIntersect(LocalRay, BoundsMin, BoundsMax, ClosestDistance, AABBDistance))
	{
		return;
	}

	// 텍스트·플립북처럼 AABB만으로 선택하는 프리미티브를 처리한다.
	if (Primitive->IsAABBOnlyPickable())
	{
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

	// 메시 BVH를 탐색해 가장 가까운 삼각형 교차점을 찾는다.
	if (PickMeshBVHNodeRecursive(MeshBVH.GetRoot(), MeshBVH, *Mesh, LocalRay, ClosestDistance))
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

void FObjectPicker::PickBVHNodeRecursive(const FSceneBVHNode* Node, const FRay& Ray, float& ClosestDistance, USceneComponent*& SelectedObject, float EnterDistance)
{
	if (Node == nullptr)
	{
		return;
	}

	// 아직 검사하지 않은 루트 노드의 AABB를 검사한다.
	if (EnterDistance < 0.0f)
	{
		if (!RayAABBIntersect(Ray, Node->WorldMin, Node->WorldMax, ClosestDistance, EnterDistance))
		{
			return;
		}
	}

	// 앞선 탐색에서 더 가까운 교차점을 찾았다면 이 노드를 제외한다.
	if (EnterDistance > ClosestDistance)
	{
		return;
	}

	// 리프에 도달하면 해당 메시를 정확히 검사한다.
	if (Node->ComponentOrNull != nullptr)
	{
		TestPrimitive(Node->ComponentOrNull, Ray, ClosestDistance, SelectedObject);
		return;
	}

	const FSceneBVHNode* FirstChild = Node->LeftChild;
	const FSceneBVHNode* SecondChild = Node->RightChild;

	float FirstDistance = 0.0f;
	float SecondDistance = 0.0f;

	// 두 자식의 AABB를 검사하고 진입 거리를 구한다.
	{
		if (FirstChild != nullptr)
		{
			if (!RayAABBIntersect(Ray, FirstChild->WorldMin, FirstChild->WorldMax, ClosestDistance, FirstDistance))
			{
				FirstChild = nullptr;
			}
		}

		if (SecondChild != nullptr)
		{
			if (!RayAABBIntersect(Ray, SecondChild->WorldMin, SecondChild->WorldMax, ClosestDistance, SecondDistance))
			{
				SecondChild = nullptr;
			}
		}
	}

	// 교차하는 자식 중 가까운 자식을 먼저 방문하도록 순서를 정한다.
	{
		if (SecondChild != nullptr && (FirstChild == nullptr || SecondDistance < FirstDistance))
		{
			const FSceneBVHNode* TempChild = FirstChild;
			FirstChild = SecondChild;
			SecondChild = TempChild;

			const float TempDistance = FirstDistance;
			FirstDistance = SecondDistance;
			SecondDistance = TempDistance;
		}
	}

	// 가까운 자식을 탐색해 최단 교차 거리를 먼저 갱신한다.
	if (FirstChild != nullptr)
	{
		PickBVHNodeRecursive(FirstChild, Ray, ClosestDistance, SelectedObject, FirstDistance);
	}

	// 갱신된 최단 거리 안에 있는 경우에만 먼 자식을 탐색한다.
	if (SecondChild != nullptr && SecondDistance <= ClosestDistance)
	{
		PickBVHNodeRecursive(SecondChild, Ray, ClosestDistance, SelectedObject, SecondDistance);
	}
}

bool FObjectPicker::PickMeshBVHNodeRecursive(const FMeshBVHNode* Node, const FMeshBVH& BVH, const FMeshResource& Mesh, const FRay& LocalRay, float& ClosestDistance, float EnterDistance)
{
	if (Node == nullptr)
	{
		return false;
	}

	// 아직 검사하지 않은 루트 노드의 AABB를 검사한다.
	if (EnterDistance < 0.0f)
	{
		if (!RayAABBIntersect(LocalRay, Node->LocalMin, Node->LocalMax, ClosestDistance, EnterDistance))
		{
			return false;
		}
	}

	// 앞선 탐색에서 더 가까운 교차점을 찾았다면 이 노드를 제외한다.
	if (EnterDistance > ClosestDistance)
	{
		return false;
	}

	// 리프에 배정된 삼각형만 정확히 검사한다.
	if (Node->TrianglesCount > 0)
	{
		const TArray<uint32>& TriangleOrders = BVH.GetTriangleOrders();
		const TArray<uint32>& Indices = Mesh.GetIndices();
		const TArray<FVector>& Positions = Mesh.GetPositions();

		bool bHit = false;
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

		return bHit;
	}

	const FMeshBVHNode* FirstChild = Node->LeftChild;
	const FMeshBVHNode* SecondChild = Node->RightChild;

	float FirstDistance = 0.0f;
	float SecondDistance = 0.0f;

	// 두 자식의 AABB를 검사하고 진입 거리를 구한다.
	{
		if (FirstChild != nullptr)
		{
			if (!RayAABBIntersect(LocalRay, FirstChild->LocalMin, FirstChild->LocalMax, ClosestDistance, FirstDistance))
			{
				FirstChild = nullptr;
			}
		}

		if (SecondChild != nullptr)
		{
			if (!RayAABBIntersect(LocalRay, SecondChild->LocalMin, SecondChild->LocalMax, ClosestDistance, SecondDistance))
			{
				SecondChild = nullptr;
			}
		}
	}

	// 교차하는 자식 중 가까운 자식을 먼저 방문하도록 순서를 정한다.
	{
		if (SecondChild != nullptr && (FirstChild == nullptr || SecondDistance < FirstDistance))
		{
			const FMeshBVHNode* TempChild = FirstChild;
			FirstChild = SecondChild;
			SecondChild = TempChild;

			const float TempDistance = FirstDistance;
			FirstDistance = SecondDistance;
			SecondDistance = TempDistance;
		}
	}

	bool bHit = false;

	// 가까운 자식의 삼각형부터 검사한다.
	if (FirstChild != nullptr)
	{
		bHit = PickMeshBVHNodeRecursive(FirstChild, BVH, Mesh, LocalRay, ClosestDistance, FirstDistance);
	}

	// 더 가까운 교차점이 있을 수 있는 경우에만 먼 자식을 탐색한다.
	if (SecondChild != nullptr && SecondDistance <= ClosestDistance)
	{
		if (PickMeshBVHNodeRecursive(SecondChild, BVH, Mesh, LocalRay, ClosestDistance, SecondDistance))
		{
			bHit = true;
		}
	}

	return bHit;
}