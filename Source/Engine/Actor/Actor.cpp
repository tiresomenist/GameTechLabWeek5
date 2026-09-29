#include "pch.h"
#include "Actor.h"

#include "Engine/Object/ClassType.h"
#include "Engine/Object/ObjectFactory.h"
#include "Engine/Component/SceneComponent.h"
#include "Core/Serialization/Archive.h"
#include "Engine/Scene/Scene.h"

UActorComponent* AActor::CreateComponent(FClassType* Type, uint32 UUID)
{
    if (!Type || !Type->IsA(UActorComponent::GetClass()))
    {
        return nullptr;
    }

    UActorComponent* Component = static_cast<UActorComponent*>(FObjectFactory::ConstructSceneObject(Type, UUID));

    Component->SetOwner(this);
    Components.Add(Component);

    if (Component->IsA(USceneComponent::GetClass()))
    {
        USceneComponent* SceneComponent = static_cast<USceneComponent*>(Component);
        if (!RootComponent && SceneComponent->CanBeRootComponent())
        {
            RootComponent = SceneComponent;
        }
        else if (RootComponent && SceneComponent != RootComponent)
        {
            // Root가 될 수 없는 보조 SceneComponent도 Transform 계층에는 포함한다.
            SceneComponent->AttachTo(RootComponent);
        }
    }

    Component->OnRegister();
    if (Scene)Scene->RegisterComponent(Component);
    if (bHasBegunPlay)
    {
        Component->BeginPlay();
    }

    return Component;
}

bool AActor::RemoveComponent(UActorComponent* Component, bool bDestroy)
{
    if (Component == nullptr || Component->GetOwner() != this)
    {
        return false;
    }

    if (bHasBegunPlay)
    {
        Component->EndPlay();
    }

    if (Scene) Scene->UnregisterComponent(Component);

    Component->OnUnregister();

    if (Component->IsA(USceneComponent::GetClass()))
    {
        USceneComponent* SceneComponent = static_cast<USceneComponent*>(Component);

        const TArray<USceneComponent*> Children = SceneComponent->GetAttachChildren();
        for (USceneComponent* Child : Children)
            Child->DetachFromParent();

        SceneComponent->DetachFromParent();
    }

    Components.Remove(Component);

    if (RootComponent == Component)
    {
        RootComponent = nullptr;

        // Root를 제거해도 Actor에 남아 있는 SceneComponent가 있으면 새 Root로 승격한다.
        for (UActorComponent* RemainingComponent : Components)
        {
            if (RemainingComponent->IsA(USceneComponent::GetClass()))
            {
                USceneComponent* Candidate = static_cast<USceneComponent*>(RemainingComponent);
                if (Candidate->CanBeRootComponent())
                {
                    RootComponent = Candidate;
                    break;
                }
            }
        }
    }

    Component->SetOwner(nullptr);
    if (bDestroy)
    {
        delete Component;
    }

    return true;
}

void AActor::SetRootComponent(USceneComponent* Component)
{
    if (Component == nullptr || Component->GetOwner() != this)
    {
        return;
    }

    RootComponent = Component;
}

bool AActor::SetParentActor(AActor* NewParent)
{
    if (NewParent == this)
    {
        return false;
    }

    // 새 부모의 상위 체인에 자신이 있으면 순환 계층이 된다.
    for (AActor* Ancestor = NewParent; Ancestor != nullptr; Ancestor = Ancestor->ParentActor)
    {
        if (Ancestor == this)
        {
            return false;
        }
    }

    if (ParentActor == NewParent)
    {
        return true;
    }

    if (ParentActor != nullptr)
    {
        TArray<AActor*>& Siblings = ParentActor->ChildActors;
        for (int32 Index = 0; Index < Siblings.Num(); ++Index)
        {
            if (Siblings[Index] == this)
            {
                Siblings.RemoveAt(Index);
                break;
            }
        }
    }

    ParentActor = NewParent;
    if (ParentActor != nullptr)
    {
        ParentActor->ChildActors.Add(this);
    }
    return true;
}

void AActor::DetachChildren()
{
    for (AActor* Child : ChildActors)
    {
        if (Child != nullptr && Child->ParentActor == this)
        {
            Child->ParentActor = nullptr;
        }
    }
    ChildActors.Empty();
}

void AActor::BeginPlay()
{
    if (bHasBegunPlay)
    {
        return;
    }

    bHasBegunPlay = true;
    for (UActorComponent* Component : Components)
    {
        Component->BeginPlay();
    }
}

void AActor::Tick(float DeltaTime)
{
    // 이제 Actor에서 Component들의 틱을 도는게 아닌, 틱 그룹에서 Register되어서 틱을 돌게됨.
    //for (UActorComponent* Component : Components)
    //{
    //    Component->Tick(DeltaTime);
    //}
}

void AActor::EndPlay()
{
    if (!bHasBegunPlay)
    {
        return;
    }

    for (UActorComponent* Component : Components)
    {
        Component->EndPlay();
    }
    bHasBegunPlay = false;
}

void AActor::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);

    bool bVisibleValue = bVisible;
	Archive.OptionalField("bVisible", bVisibleValue);

    if (Archive.IsLoading())
    {
		bVisible = bVisibleValue;
    }
}

void AActor::ReleaseComponents()
{
    for (UActorComponent* Component : Components)
    {
        if (Scene) Scene->UnregisterComponent(Component);
        Component->OnUnregister();
        Component->SetOwner(nullptr);
        delete Component;
    }

    Components.Empty();
    RootComponent = nullptr;
}

AActor::~AActor()
{
    SetParentActor(nullptr);
    DetachChildren();
    EndPlay();
    ReleaseComponents();
}

// Actor 자체 Tick을 활성화하거나 비활성화합니다.
void AActor::SetActorTickEnabled(bool bEnabled)
{
    const bool bNewEnabled = PrimaryActorTick.bCanEverTick && bEnabled;
    if (PrimaryActorTick.bTickEnabled == bNewEnabled) return;

    PrimaryActorTick.bTickEnabled = bNewEnabled;
    if (Scene) Scene->RefreshActorTick(this);
}

// 실행 그룹이 바뀌면 기존 그룹에서 새 그룹으로 등록을 이동합니다.
void AActor::SetActorTickGroup(ETickGroup Group)
{
    if (Group >= ETickGroup::Count || PrimaryActorTick.TickGroup == Group) return;

    PrimaryActorTick.TickGroup = Group;
    if (Scene) Scene->RefreshActorTick(this);
}
