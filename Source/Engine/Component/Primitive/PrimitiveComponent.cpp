#include "pch.h"
#include "PrimitiveComponent.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/MeshResource.h"

void UPrimitiveComponent::Initialize()
{
	Super::Initialize();
}

bool UPrimitiveComponent::GetLocalBounds(FVector& OutMin, FVector& OutMax) const
{
	// 기본: 기저 컴포넌트는 고유한 형상이 없으므로 false 반환.
	// 형상이 있는 자식 컴포넌트(UStaticMeshComponent, UTextComponent 등)에서 오버라이드함.
	return false;
}

const FMatrix& UPrimitiveComponent::GetRenderWorldMatrix(const UCameraComponent* Camera) const
{
	return GetWorldMatrix();
}


void UPrimitiveComponent::SubmitLineDrawRequests(const FLineDrawContext& Context, const FLineRequestConsumer& Submit) const
{
    if (!Context.bShowBounds && !Context.bSelected)
    {
        return;
    }

    FVector Min;
    FVector Max;

    if (!GetLocalBounds(Min, Max))
    {
        return;
    }

    FLineDrawRequest Request;
    Request.Vertices.Reserve(8);
    Request.Indices.Reserve(24);

    // 빌보드 오브젝트도 실제 표시되는 자세에 맞춰 박스를 생성함
    const FMatrix& World = GetRenderWorldMatrix(Context.Camera);

    for (uint32 Corner = 0; Corner < 8; ++Corner)
    {
        const FVector Local(
            (Corner & 1) ? Max.X : Min.X,
            (Corner & 2) ? Max.Y : Min.Y,
            (Corner & 4) ? Max.Z : Min.Z);

        const FVector Position = World.TransformPosition(Local);

        Request.Vertices.Add({ Position.X, Position.Y, Position.Z,1.0f, 1.0f, 0.0f, 1.0f });
    }

    // 한 축의 비트만 다른 두 꼭짓점을 연결함
    for (uint32 Corner = 0; Corner < 8; ++Corner)
    {
        for (uint32 Bit = 1; Bit <= 4; Bit <<= 1)
        {
            if ((Corner & Bit) == 0)
            {
                Request.Indices.Add(Corner);
                Request.Indices.Add(Corner | Bit);
            }
        }
    }
    Submit(Request);
}
