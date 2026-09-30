#pragma once

#include "Core/Container/Array.h"
#include "Engine/Component/ActorComponent.h"
#include "Engine/Scene/Tick.h"

class USceneComponent;
struct FClassType;
class UScene;


// Scene에 배치되는 게임 오브젝트 단위입니다. Component의 생성과 파괴를 소유합니다.
class AActor : public UObject
{
   UCLASS(AActor, "Actor", UObject)

public:
    UActorComponent* CreateComponent(FClassType* Type, uint32 UUID = -1);
    bool RemoveComponent(UActorComponent* Component, bool bDestroy = true);

    void SetRootComponent(USceneComponent* Component);
    USceneComponent* GetRootComponent() const { return RootComponent; }

    const TArray<UActorComponent*>& GetComponents() const { return Components; }

    // 삭제 수명을 공유하는 Actor 계층입니다. Transform 계층은 각 Actor 내부 Component가 담당합니다.
    bool SetParentActor(AActor* NewParent);
    AActor* GetParentActor() const { return ParentActor; }
    const TArray<AActor*>& GetChildActors() const { return ChildActors; }

    virtual void BeginPlay();
    virtual void Tick(float DeltaTime);
    virtual void EndPlay();

    virtual void Serialize(FArchive& Archive) override;

    bool IsVisible() const { return bVisible; }
    void SetVisibility(bool bIsVisible) { bVisible = bIsVisible; }

    ~AActor() override;
    UScene* GetScene()const { return Scene; }

    // Actor 자체 Tick의 활성 상태를 변경합니다.
    void SetActorTickEnabled(bool bEnabled);

    // Actor 자체 Tick의 실행 그룹을 변경합니다.
    void SetActorTickGroup(ETickGroup Group);
protected:
    FTickSettings PrimaryActorTick;
private:
    void ReleaseComponents();
    void DetachChildren();

    TArray<UActorComponent*> Components;
    USceneComponent* RootComponent = nullptr;
    AActor* ParentActor = nullptr;
    TArray<AActor*> ChildActors;
    bool bHasBegunPlay = false;
    bool bVisible = true;
    UScene* Scene = nullptr;
    friend class UScene;
};
