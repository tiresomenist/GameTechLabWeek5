#pragma once

#include "Engine/Object/Object.h"
#include "Engine/Scene/Tick.h"

class AActor;
class UScene;


// Actor에 소속되는 기능 단위입니다. Owner는 Actor만 설정할 수 있습니다.
class UActorComponent : public UObject
{
    UCLASS(UActorComponent, "ActorComponent", UObject)

public:
    AActor* GetOwner() const { return Owner; }

    // Owner가 설정된 뒤 한 번 호출됩니다. Owner가 필요한 초기화는 여기서 합니다.
    virtual void OnRegister() {}
    // Owner가 아직 유효한 상태에서 호출됩니다.
    virtual void OnUnregister() {}

    virtual void BeginPlay() {}
    virtual void Tick(float DeltaTime) {}
    virtual void EndPlay() {}

    // Component Tick의 활성 상태를 변경합니다.
    void SetComponentTickEnabled(bool bEnabled);

    // Component Tick의 실행 그룹을 변경합니다.
    void SetComponentTickGroup(ETickGroup Group);

protected:
    FTickSettings PrimaryComponentTick;

private:
    AActor* Owner = nullptr;

    void SetOwner(AActor* InOwner) { Owner = InOwner; }
    bool bRegisteredWithScene = false;

    friend class UScene;
    friend class AActor;
};
