#include "pch.h"
#include "TracerComponent.h"
#include "Core/Math/Quaternion.h"
#include "Engine/Actor/Actor.h"
#include "Engine/Component/SceneComponent.h"
#include "Engine/Component/Primitive/FlipbookComponent.h"
#include "Engine/Scene/Scene.h"

void UTracerComponent::Tick(float DeltaTime)
{
	if (!OwnerTransform)
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			return;
		}

		OwnerTransform = Owner->GetRootComponent();
		if (!OwnerTransform)
		{
			return;
		}
	}

	if (!TargetTransform)
	{
		return;
	}

	FVector O = OwnerTransform->GetRelativeLocation();
	FVector T = TargetTransform->GetRelativeLocation();
	FVector Direction = T - O;

	// 이동
	Direction.Normalize();
	OwnerTransform->SetRelativeLocation(O + Direction * TraceSpeed * DeltaTime);
	OwnerTransform->SetRelativeRotation(FQuaternion::FromToRotation(FVector(0.0f, 0.0f, 1.0f), Direction));
}

void UTracerComponent::SetTarget(USceneComponent* Target)
{
	TargetTransform = Target;
}

void UTracerComponent::SetSpeed(float Speed)
{
	TraceSpeed = Speed;
}
