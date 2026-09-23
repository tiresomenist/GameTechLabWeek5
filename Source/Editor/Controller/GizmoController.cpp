#include "pch.h"
#include "Editor/Controller/GizmoController.h"
#include "Editor/Editor.h"
#include "Engine/Component/CameraComponent.h"
#include <cmath>
#include "Engine/Input/InputManager.h"
#include "Engine/Component/Primitive/PrimitiveComponent.h"
#include "Editor/Gizmo/ObjectAxisGizmo.h"


namespace
{
	bool WorldToPixel(const FVector& Position,const FMatrix& ViewProjection,const D3D11_VIEWPORT& Viewport, FVector& OutPixel)
	{
		const FVector4 Clip = FVector4(Position, 1.0f) * ViewProjection;

		if (!std::isfinite(Clip.W) || Clip.W <= 1.0e-6f)
			return false;

		const float NDCX = Clip.X / Clip.W;
		const float NDCY = Clip.Y / Clip.W;

		OutPixel = FVector(
			Viewport.TopLeftX + (NDCX + 1.0f) * 0.5f * Viewport.Width,
			Viewport.TopLeftY + (1.0f - NDCY) * 0.5f * Viewport.Height,	0.0f
		);

		return std::isfinite(OutPixel.X)&& std::isfinite(OutPixel.Y);
	}

    struct FMouseRay
	{
        FVector Origin;
        FVector Direction;
	};

    FMouseRay GetMouseRay(const FVector2& Position, const FMatrix& ViewProjection, const D3D11_VIEWPORT& Viewport)
    {
		float NDCX = 2.0f * (Position.X - Viewport.TopLeftX) / Viewport.Width - 1.0f;
		float NDCY = 1.0f - 2.0f * (Position.Y - Viewport.TopLeftY) / Viewport.Height;

		FVector4 Near(NDCX, NDCY, 0.0f, 1.0f);
		FVector4 Far(NDCX, NDCY, 1.0f, 1.0f);
        
        FMatrix VPInverse;
		if (!ViewProjection.TryInverse(VPInverse)) {
            return {};
		}

		FVector4 NearWorld = Near * VPInverse;	//World에서의 Ray 시작점
		FVector4 FarWorld = Far * VPInverse; //World에서의 Ray 끝점

		if (std::fabs(NearWorld.W) < 1.0e-6f || std::fabs(FarWorld.W) < 1.0e-6f) return {};

		NearWorld = FVector4(FVector(NearWorld) / NearWorld.W, 1.0f);
		FarWorld = FVector4(FVector(FarWorld) / FarWorld.W, 1.0f);

        return FMouseRay {
            .Origin = FVector(NearWorld),
            .Direction = (FVector(FarWorld) - FVector(NearWorld)).GetNormalized()
        };
    }
}

FGizmoController::FGizmoController(FEditor* InEditor)
    : Editor(InEditor)
{
    WorldDirections.SetNum(3);
    ScreenDirections.SetNum(3);
    if (Editor != nullptr)
    {
        ObjectAxisGizmo =
            dynamic_cast<UObjectAxisGizmo*>(Editor->GetObjectAxisGizmo());
    }
}

FGizmoController::~FGizmoController()
{
}



void FGizmoController::CalculateAxis()
{
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        WorldDirections[Axis] = FVector(0, 0, 0);
        ScreenDirections[Axis] = FVector(0, 0, 0);
    }

    if (!Editor || !SelectedObject || !ObjectAxisGizmo) //뭔가 잘못되었으면
        return;

    UCameraComponent* Camera = Editor->GetEditorCamera();
    if (!Camera) return;    //카메라를 못받아왔으면

	uint32 ViewportIndex = Editor->GetCurrentEditViewportIndex();
    if (ViewportIndex >= Editor->GetViewports().Num()) return;
    const FViewportClient& ViewportClient = Editor->GetViewports()[ViewportIndex];
    const auto& Viewport = ViewportClient.GetViewportInfo();
    if (Viewport.Width <= 0.0f || Viewport.Height <= 0.0f) return; //창 크기가 0보다 작으면

    Camera->SetAspectRatio(Viewport.Width / Viewport.Height);

    FMatrix ViewProjection = Camera->GetViewMatrix() * Camera->GetProjectionMatrix();

    // 현재 프로젝트는 UpdateTransform()에서 축별 행렬을 갱신
    if (!ObjectAxisGizmo->UpdateTransform()) return;

    TArray<FMatrix> AxisMatrices = {
        ObjectAxisGizmo->GetXAxisWorldMatirx(),
        ObjectAxisGizmo->GetYAxisWorldMatirx(),
        ObjectAxisGizmo->GetZAxisWorldMatirx()
    };


    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FVector WorldOrigin(FVector4(0, 0, 0, 1) * AxisMatrices[Axis]); //월드기준 화살표 원점

        FVector WorldEnd(FVector4(0, 0, 1, 1) * AxisMatrices[Axis]);    //월드기준 화살표 끝점

        FVector WorldDelta = WorldEnd - WorldOrigin;
        float WorldLength = WorldDelta.Length();

        if (!std::isfinite(WorldLength) || WorldLength <= 1.0e-6f)
            continue;

        WorldDirections[Axis] = WorldDelta / WorldLength;

        FVector ScreenOrigin;
        FVector ScreenEnd;

        //월드좌표계->화면좌표계
        if (!WorldToPixel(WorldOrigin, ViewProjection, Viewport, ScreenOrigin) ||
            !WorldToPixel(WorldEnd, ViewProjection, Viewport, ScreenEnd))
        {
            continue;
        }

        //화면기준 기즈모 방향
        const FVector ScreenDelta = ScreenEnd - ScreenOrigin;
        const float PixelLength = ScreenDelta.Length();

        // 축이 너무 짧아서 땡기기 힘들때
        if (!std::isfinite(PixelLength) || PixelLength < 2.0f)
            continue;

        ScreenDirections[Axis] = ScreenDelta / PixelLength;
    }
}

void FGizmoController::SetSelectedObject(USceneComponent* InObject)
{
	EndDrag();
	SelectedObject = InObject;
}

//에디터에서 드래그 입력 있고, 축 선택 성공시.
bool FGizmoController::BeginDrag(int32 Axis)
{
	EndDrag();
	if (Editor) ObjectAxisGizmo = dynamic_cast<UObjectAxisGizmo*>(Editor->GetObjectAxisGizmo());
	if (!SelectedObject || !ObjectAxisGizmo || Axis < 0 || Axis > 2) return false;

	CalculateAxis();
	if (Mode != EGizmoMode::Rotate && ScreenDirections[Axis].LengthSquared() <= 1.0e-6f) return false;
	if (WorldDirections[Axis].LengthSquared() <= 1.0e-6f) return false;

	ActiveAxis = Axis;
	StartObjectLocation = SelectedObject->GetRelativeLocation();
    StartObjectRotation = SelectedObject->GetRelativeRotation();
    StartObjectScale = SelectedObject->GetRelativeScale3D();
	DragPixels = FVector(0, 0, 0);
	DragScreenDirection = ScreenDirections[Axis];
	DragWorldDirection = WorldDirections[Axis];

    auto& Input = *GInputManager::GetInstance();
    const UCameraComponent* Camera = Editor->GetEditorCamera();
    if (!Camera) return false;

    uint32 ViewportIndex = Editor->GetCurrentEditViewportIndex();
    if (ViewportIndex >= Editor->GetViewports().Num()) return false;
    const FViewportClient& ViewportClient = Editor->GetViewports()[ViewportIndex];
    const auto& Viewport = ViewportClient.GetViewportInfo();
    if (Viewport.Width <= 0.0f || Viewport.Height <= 0.0f) return false; //창 크기가 0보다 작으면

	const FMatrix ViewProjection = Camera->GetViewMatrix() * Camera->GetProjectionMatrix();

	if (Mode == EGizmoMode::Translate)
	{

        DragPlaneOrigin = SelectedObject->GetWorldMatrix().GetOrigin();
        
        if (!WorldToPixel(DragPlaneOrigin, ViewProjection, Viewport, DragPivotPixel))
        {
            return false;
        }

        const FVector CursorPixel(float(Input.GetLeftCursorPixelX()), float(Input.GetLeftCursorPixelY()), 0.0f);
        DragCursorOffset = CursorPixel - DragPivotPixel;

        const FMouseRay PivotRay = GetMouseRay(FVector2(DragPivotPixel.X, DragPivotPixel.Y), ViewProjection, Viewport);

        const FVector Normal = PivotRay.Direction - DragWorldDirection * PivotRay.Direction.Dot(DragWorldDirection);

        const float NormalLengthSquared = Normal.LengthSquared();
        if (!std::isfinite(NormalLengthSquared) || NormalLengthSquared < 1.0e-8f)
        {
            return false;
        }

        DragPlaneNormal = Normal / std::sqrt(NormalLengthSquared);
	}
	else if (Mode == EGizmoMode::Rotate)
	{
		const FVector PivotWorld = SelectedObject->GetWorldMatrix().GetOrigin();
		if (!WorldToPixel(PivotWorld, ViewProjection, Viewport, RotationPivotPixel))
		{
			return false;
		}

		const FMouseRay PivotRay = GetMouseRay(FVector2(RotationPivotPixel.X, RotationPivotPixel.Y), ViewProjection, Viewport);
		const float Facing = PivotRay.Direction.Dot(DragWorldDirection);
		if (!std::isfinite(Facing) || std::fabs(Facing) < 1.0e-6f)
		{
			return false;
		}

		RotationSign = (Facing > 0.0f) ? 1.0f : -1.0f;

		AccumulatedAngle = 0.0;
		bHasRotationDirection = GetRotationDirection(Input.GetLeftCursorPixelX(), Input.GetLeftCursorPixelY(), PreviousRotationDirection);
		if (!bHasRotationDirection)
		{
            EndDrag();
			return false;
		}
	}
	bDragging = true;
	return true;
}

void FGizmoController::EndDrag()
{
	bDragging = false;
	ActiveAxis = -1;
	bHasRotationDirection = false;
}

void FGizmoController::ChangeMod()
{
    EndDrag();
    if (!ObjectAxisGizmo) return;
    switch (Mode)
    {
    case EGizmoMode::Translate:
        Mode = EGizmoMode::Rotate;
        break;

    case EGizmoMode::Rotate:
        Mode = EGizmoMode::Scale;
        break;

    case EGizmoMode::Scale:
        Mode = EGizmoMode::Translate;
        break;
    }
    ObjectAxisGizmo->SetMode(Mode);
}

void FGizmoController::Tick()
{
    UCameraComponent* Camera = Editor->GetEditorCamera();
    if (!Camera) return;

    uint32 ViewportIndex = Editor->GetCurrentEditViewportIndex();
    if (ViewportIndex >= Editor->GetViewports().Num()) return;
    const FViewportClient& ViewportClient = Editor->GetViewports()[ViewportIndex];
    const auto& Viewport = ViewportClient.GetViewportInfo();
    if (Viewport.Width <= 0.0f || Viewport.Height <= 0.0f) return; //창 크기가 0보다 작으면

	auto& Input = *GInputManager::GetInstance();
	int32 DeltaX = 0, DeltaY = 0;
	Input.ConsumeLeftDragDelta(DeltaX, DeltaY);

	if (!bDragging) return;
	if (!SelectedObject || !ObjectAxisGizmo)
	{
		EndDrag();
		return;
	}

	DragPixels.X += float(DeltaX);
	DragPixels.Y += float(DeltaY);

    FVector NewScale = StartObjectScale;

    switch (Mode)
    {
    case EGizmoMode::Translate:
    {
        const FVector CursorPixel(float(Input.GetLeftCursorPixelX()), float(Input.GetLeftCursorPixelY()), 0.0f);
        const FVector DesiredPivotPixel = CursorPixel - DragCursorOffset;

        const float AlongPixels = (DesiredPivotPixel - DragPivotPixel).Dot(DragScreenDirection);

        const FVector TargetPivotPixel = DragPivotPixel + DragScreenDirection * AlongPixels;

        const FMouseRay MouseRay = GetMouseRay(FVector2(TargetPivotPixel.X, TargetPivotPixel.Y), Camera->GetViewMatrix() * Camera->GetProjectionMatrix(), Viewport);

        const float Denominator = MouseRay.Direction.Dot(DragPlaneNormal);
        if (!std::isfinite(Denominator) || std::fabs(Denominator) < 1.0e-4f) break;

        const float T = (DragPlaneOrigin - MouseRay.Origin).Dot(DragPlaneNormal) / Denominator;

        if (!std::isfinite(T) || T < 0.0f) break;

        const FVector CurrentHit = MouseRay.Origin + MouseRay.Direction * T;
        const float Distance = (CurrentHit - DragPlaneOrigin).Dot(DragWorldDirection);
        if (!std::isfinite(Distance)) break;

        const FVector WorldDelta = DragWorldDirection * Distance;
        FVector LocalDelta = WorldDelta;

        // 기즈모 축은 월드 공간 기준이다. 부모가 회전/스케일된 자식은
        // 월드 이동량을 부모 로컬 공간으로 변환한 뒤 RelativeLocation에 반영한다.
        if (USceneComponent* Parent = SelectedObject->GetAttachParent())
        {
            FMatrix InverseParentWorld;
            if (!Parent->GetWorldMatrix().TryInverse(InverseParentWorld))
            {
                break;
            }

            const FVector4 ParentLocalDelta = FVector4(WorldDelta, 0.0f) * InverseParentWorld;
            LocalDelta = FVector(ParentLocalDelta.X, ParentLocalDelta.Y, ParentLocalDelta.Z);
        }

        SelectedObject->SetRelativeLocation(StartObjectLocation + LocalDelta);
        break;
    }

    case EGizmoMode::Rotate:
    {
        FVector Direction;
        if (!GetRotationDirection(Input.GetLeftCursorPixelX(), Input.GetLeftCursorPixelY(), Direction))
        {
            bHasRotationDirection = false;
            break;
        }

        if (bHasRotationDirection)
        {
            const float SinAngle = PreviousRotationDirection.X * Direction.Y - PreviousRotationDirection.Y * Direction.X;
            const float CosAngle = PreviousRotationDirection.Dot(Direction);

            AccumulatedAngle += RotationSign * std::atan2(SinAngle, CosAngle);
			const float Angle = float(std::remainder(AccumulatedAngle, 2.0 * PI));

			const FQuaternion DeltaRotation = FQuaternion::FromAxisAngle(DragWorldDirection, Angle);
            SelectedObject->SetRelativeRotation(DeltaRotation * StartObjectRotation);
        }
        PreviousRotationDirection = Direction;
        bHasRotationDirection = true;
        break;
    }
    case EGizmoMode::Scale:
    {
        //축방향으로 움직인 값. 내적으로 구함
        const float AlongPixels = DragPixels.Dot(DragScreenDirection);
        constexpr float PixelsPerScaleUnit = 100.0f;

        //스케일 변화량
        const float Factor = (std::max)(0.01f, 1.0f + AlongPixels / PixelsPerScaleUnit);

        switch (ActiveAxis)
        {
        case 0: NewScale.X *= Factor; break;
        case 1: NewScale.Y *= Factor; break;
        case 2: NewScale.Z *= Factor; break;
        }
        SelectedObject->SetRelativeScale3D(NewScale);
        break;
    }
    }

	if (!Input.GetKey(GInputManager::EI_LMOUSE)) EndDrag();
}

bool FGizmoController::GetRotationDirection(float PixelX, float PixelY, FVector& OutDirection) const
{
    const FVector Direction(PixelX - RotationPivotPixel.X, RotationPivotPixel.Y - PixelY, 0.0f);
    const float LengthSquared = Direction.LengthSquared();

    constexpr float MinRadiusPixels = 4.0f;
    if (!std::isfinite(LengthSquared) || LengthSquared < MinRadiusPixels * MinRadiusPixels) return false;

    OutDirection = Direction / std::sqrt(LengthSquared);
    return true;
}


