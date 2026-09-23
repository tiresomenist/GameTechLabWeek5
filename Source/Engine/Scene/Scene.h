#pragma once
#include <memory>
#include "Core/Container/Array.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Object/ObjectFactory.h"
#include "Engine/Renderer/RenderUtil.h"
#include "Engine/Scene/SceneType.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Engine/Component/WidgetComponent.h"

struct FPrimitiveRenderData;
class UCameraComponent;
class FRenderer;
class FArchive;

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
		if (Type == nullptr || !Type->IsA(AActor::GetClass()))
		{
			return nullptr;
		}

		AActor* Actor = static_cast<AActor*>(FObjectFactory::ConstructSceneObject(Type, UUID));
		Actors.Add(Actor);
		return static_cast<T>(Actor);
	}

	void Destroy(UObject* Object);
	void DestroyActor(AActor* Actor);

	//외부에서 Primitive 접근 제공
	template <typename Func>
	void ForEachPrimitive(Func&& Function) const
	{
		for (AActor* Actor : Actors)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component->IsA(UPrimitiveComponent::GetClass()))
				{
					Function(static_cast<UPrimitiveComponent*>(Component));
				}
			}
		}
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
		for (AActor* Actor : Actors)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component->IsA(UWidgetComponent::GetClass()))
				{
					Function(static_cast<UWidgetComponent*>(Component));
				}
			}
		}
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

public:
	friend TArray<FPrimitiveRenderData> RenderUtil::GetRenderList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera);
	friend TArray<FPrimitiveRenderData> RenderUtil::GetGizmoList(FEditor* Editor, UScene* Scene, const UCameraComponent* Camera,
		const D3D11_VIEWPORT& Viewport);
};
