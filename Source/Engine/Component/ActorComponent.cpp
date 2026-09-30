#include "pch.h"
#include "ActorComponent.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Scene/Scene.h"

// Scene에 등록된 Component의 Tick 활성 상태를 갱신합니다.
void UActorComponent::SetComponentTickEnabled(bool bEnabled)
{
    const bool bNewEnabled = PrimaryComponentTick.bCanEverTick && bEnabled;
    if (PrimaryComponentTick.bTickEnabled == bNewEnabled) return;

    PrimaryComponentTick.bTickEnabled = bNewEnabled;
    if (bRegisteredWithScene && Owner && Owner->GetScene())
        Owner->GetScene()->RefreshComponentTick(this);
}

// 실행 그룹 변경을 소속 Scene의 목록에 반영합니다.
void UActorComponent::SetComponentTickGroup(ETickGroup Group)
{
    if (Group >= ETickGroup::Count || PrimaryComponentTick.TickGroup == Group) return;

    PrimaryComponentTick.TickGroup = Group;
    if (bRegisteredWithScene && Owner && Owner->GetScene())
        Owner->GetScene()->RefreshComponentTick(this);
}
