#include "pch.h"
#include "ViewportClient.h"

void FViewportClient::Initialize(EViewportType InType, UCameraComponent* InCamera)
{
	ViewportType = InType;
	Camera = InCamera;

	switch (ViewportType)
	{
	case EViewportType::Perspective:		// 1사분면
	{
		Camera->SetIsPerspective(true);
		Camera->SetRelativeLocation(FVector(-15.0f, -15.0f, 10.0f));
		Camera->LookAt(FVector(0.0f, 0.0f, 0.0f));

		ViewSettings.ViewMode = EViewModeIndex::VMI_Lit;
		break;
	}
	case EViewportType::Top:				// 2사분면
	{
		Camera->SetIsPerspective(false);
		Camera->SetRelativeRotation(FQuaternion::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), PI * 0.5f));
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
		ViewSettings.ViewMode = EViewModeIndex::VMI_Unlit;
		break;
	}
	case EViewportType::Front:				// 3사분면
	{
		Camera->SetIsPerspective(false);
		Camera->SetRelativeRotation(FQuaternion::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), PI));
		Camera->SetRelativeLocation(FVector(30.0f, 0.0f, 0.0f));
		ViewSettings.ViewMode = EViewModeIndex::VMI_Unlit;
		break;
	}
	case EViewportType::Right:				// 4사분면
	{
		Camera->SetIsPerspective(false);
		Camera->SetRelativeRotation(FQuaternion::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), -PI * 0.5f));
		Camera->SetRelativeLocation(FVector(0.0f, 30.0f, 0.0f));
		ViewSettings.ViewMode = EViewModeIndex::VMI_Unlit;
		break;
	}
	}
}

void FViewportClient::SetRect(float x, float y, float Width, float Height)
{
	ViewportInfo.TopLeftX = x;
	ViewportInfo.TopLeftY = y;
	ViewportInfo.Width = Width;
	ViewportInfo.Height = Height;
	ViewportInfo.MinDepth = 0.0f;
	ViewportInfo.MaxDepth = 1.0f;
}

FRenderView FViewportClient::GetRenderView() const
{
	FRenderView result;
	result.bDrawEditorGizmos = bDrawEditorGizmos;
	result.Camera = Camera;
	result.ViewSettings = ViewSettings;
	result.Viewport = ViewportInfo;
	result.ViewType = ViewportType;
	return result;
}

UCameraComponent* FViewportClient::GetCamera() const
{
	return Camera;
}

bool FViewportClient::IsMouseInside(float ScreenX, float ScreenY) const
{
	float LeftX = ViewportInfo.TopLeftX;
	float RightX = ViewportInfo.TopLeftX + ViewportInfo.Width;

	float TopY = ViewportInfo.TopLeftY;
	float BottomY = ViewportInfo.TopLeftY + ViewportInfo.Height;

	return (ScreenX >= LeftX) && (ScreenX < RightX) && (ScreenY >= TopY) && (ScreenY < BottomY);
}
