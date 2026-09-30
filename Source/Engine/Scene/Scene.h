#pragma once
#include <memory>
#include "Core/Container/Array.h"
#include "Core/Math/Box.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Object/ObjectFactory.h"
#include "Engine/Renderer/RenderUtil.h"
#include "Engine/Scene/SceneType.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Engine/Component/WidgetComponent.h"

// Todo: BVH
#include "Engine/Scene/SceneBVH.h"

// 정적 grid 안에서 primitive와 현재 world AABB를 보관
struct FUniformGridPrimitive
{
	UPrimitiveComponent* Primitive = nullptr;
	FBoundingBox WorldBounds;
};

// 실제 Render Data를 만든 뒤, index가 채워진 cell
struct FStaticUniformGridCell
{
	uint64 Key = 0;
	FBoundingBox SpatialBounds;  // 고정 nxn
	FBoundingBox ContentBounds;  // world AABB 합집합
	TArray<FUniformGridPrimitive> Primitives;
};

struct FPrimitiveRenderData;
class UCameraComponent;
class FRenderer;
class FArchive;
class UTextComponent;
class USpotLightComponent;

struct FCameraSaveData
{
	FVector Location = FVector(-15.0f, -15.0f, 10.0f);
	FRotator Rotation = FRotator(-25.239f, 45.0f, 0.0f);
	float FOV = 60.0f * PI / 180.0f;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
};

// Scene은 Actor를 소유하는 컨테이너입니다. UUID/RTTI가 필요한 UObject가 아닙니다.
class UScene
{
public:
	UScene() = default;

	static FSceneType* GetStaticSceneType();
	virtual FSceneType* GetSceneType() const { return GetStaticSceneType(); }

	virtual void BeginPlay();

    virtual void Tick(float DeltaTime);

	virtual void EndPlay();

	UCameraComponent* GetMainCamera() const { return MainCamera; }
	void SetMainCamera(UCameraComponent* InCamera) { MainCamera = InCamera; }
	FCameraSaveData GetMainCameraSaveData() const { return MainCameraSaveData; }
	void SetMainCameraSaveData(UCameraComponent* InCamera);

	void CreateMainCamera();

	void Serialize(FArchive& Archive);

	template <typename T>
	T SpawnActor(FClassType* Type, uint32 UUID = -1)
	{
		if (!Type || !Type->IsA(AActor::GetClass()))
		{
			return nullptr;
		}

		AActor* Actor = static_cast<AActor*>(FObjectFactory::ConstructSceneObject(Type, UUID));
		AttachActor(Actor);
		return static_cast<T>(Actor);
	}

	void Destroy(UObject* Object);
	void DestroyActor(AActor* Actor);
	const TArray<FStaticUniformGridCell>& GetStaticUniformGrid() const;
	const TArray<UPrimitiveComponent*>& GetStaticUniformGridFallback() const;
	void InvalidateStaticUniformGrid();

	//외부에서 Primitive 접근 제공
	template <typename Func>
	void ForEachPrimitive(Func&& Function) const
	{
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
			Function(Primitive);
	}

	template <typename Func>
	void ForEachActor(Func&& Function) const
	{
		for (AActor* Actor : Actors)
		{
			Function(Actor);
		}
	}

	template <typename Func>
	void ForEachWidget(Func&& Function) const
	{
		for (UWidgetComponent* Widget : WidgetComponents)
			Function(Widget);
	}
	template <typename Func>
	void ForEachText(Func&& Function) const
	{
		for (UTextComponent* Text : TextComponents)
			Function(Text);
	}

	template <typename Func>
	void ForEachNonStaticMesh(Func&& Function) const
	{
		for (UPrimitiveComponent* Primitive : NonStaticMeshComponents)
			Function(Primitive);
	}

	template <typename Func>
	void ForEachBillboardIcon(Func&& Function) const
	{
		// TODO: 공통 아이콘 타입 도입 전까지 SpotLight를 전달합니다.
		for (USpotLightComponent* SpotLight : BillboardIcons)
			Function(SpotLight);
	}

	template <typename Func>
	void ForEachSceneComponent(Func&& Function) const
	{
		for (AActor* Actor : Actors)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component->IsA(USceneComponent::GetClass()))
				{
					Function(static_cast<USceneComponent*>(Component));
				}
			}
		}
	}

	virtual ~UScene();

	// Todo: BVH
	void UpdateBVH(UStaticMeshComponent* Component);
	void UpdateBVHForActor(AActor* Actor);
	void RemoveFromBVH(UStaticMeshComponent* Component);
	bool RemoveComponent(AActor* Actor, UActorComponent* Component);
	void RebuildBVH();

	const FSceneBVHNode* GetBVHRoot() const
	{
		return BVH.GetRoot();
	}


	// 소속 Actor의 Tick 설정을 활성 목록에 반영합니다.
	void RefreshActorTick(AActor* Actor);

	// 소속 Component의 Tick 설정을 활성 목록에 반영합니다.
	void RefreshComponentTick(UActorComponent* Component);

	void UpdateStaticUniformGridForActor(AActor* Actor, TArray<uint64>& ChangedCellKeys);

private:
	FSceneBVH BVH;
	// 생성이 끝난 Actor를 이 Scene에 연결하고 기존 컴포넌트를 등록합니다.
	void AttachActor(AActor* Actor);

	// Actor의 컴포넌트 등록을 해제한 뒤 Scene 소속을 끊습니다.
	void DetachActor(AActor* Actor);

	// 컴포넌트를 해당 타입의 비소유 목록에 한 번만 등록합니다.
	void RegisterComponent(UActorComponent* Component);

	// 컴포넌트의 목록과 BVH 참조를 객체 삭제 전에 제거합니다.
	void UnregisterComponent(UActorComponent* Component);

	// Scene 파괴 시 모든 참조를 일괄 해제하고 Actor를 삭제합니다.
	void ClearActors();

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	TArray<UTextComponent*> TextComponents;
	TArray<UWidgetComponent*> WidgetComponents;
	TArray<UPrimitiveComponent*>NonStaticMeshComponents;
	// TODO: 공통 아이콘 구조 도입 후 SpotLight 전용 타입을 일반화합니다.
	TArray<USpotLightComponent*> BillboardIcons;

	TTickRegistry<AActor> ActorTicks;
	TTickRegistry<UActorComponent> ComponentTicks;

	// 역직렬화 중에는 BVH 갱신을 모으고, 복원이 끝나면 재구축합니다.
	bool bDeferBVHUpdates = false;

	friend class AActor;
protected:
	void EnsureUUIDWidgets();

	/// <summary>
	/// Scene에 종속된 모든 Actor를 담는 멤버 변수. Component는 Actor가 소유합니다.
	/// </summary>
	TArray<AActor*> Actors {};

	/// <summary>
	/// Scene의 렌더링을 담당할 MainCamera를 담는 멤버 변수
	/// </summary>
	UCameraComponent* MainCamera = nullptr;

	/// <summary>
	/// 저장/불러오기 시에 사용하는 Scene의 메인 Perspective 카메라의 정보
	/// </summary>
	FCameraSaveData MainCameraSaveData{};

	mutable bool bStaticUniformGridDirty = true;
	mutable TArray<FStaticUniformGridCell> StaticUniformGrid;
	mutable TArray<UPrimitiveComponent*> StaticUniformGridFallback;
	mutable TMap<uint64, int32> StaticUniformGridCellIndices;
	mutable TMap<UStaticMeshComponent*, uint64> StaticMeshCellKeys;

	void BuildStaticUniformGrid() const;
	void RebuildCellContentBounds(FStaticUniformGridCell& Cell) const;

public:
	friend void RenderUtil::GetRenderList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
		TArray<FPrimitiveRenderData>& RenderList, TArray<FRenderObjectData>& Objects,
		const TArray<FGridCellCandidate>& RenderGridCells, const FFrustum* Frustum);

	friend TArray<FPrimitiveRenderData> RenderUtil::GetGizmoList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
		const D3D11_VIEWPORT& Viewport, TArray<FRenderObjectData>& Objects);
};
