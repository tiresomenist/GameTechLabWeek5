#include "pch.h"
#include "CameraComponent.h"
#include "Engine/Object/Object.h"
#include <stdexcept>
#include "Core/Serialization/Archive.h"


namespace
{
    bool ValidCamera(float FOV, float Aspect, float Near, float Far, float Speed, float Height)
    {
        if (!std::isfinite(FOV) || FOV <= 0 || FOV >= PI ||
            !std::isfinite(Aspect) || Aspect <= 0 ||
            !std::isfinite(Near) || Near <= 0 ||
            !std::isfinite(Far) || Far <= Near ||
            !std::isfinite(Speed) || Speed < 0 ||
            !std::isfinite(Height) || Height <= 0) return false;
        const float P = 1.0f / std::tan(FOV * 0.5f);
        const float O = 2.0f / Height;
        const float D = Far / (Far - Near);
        const float OD = 1.0f / (Far - Near);
        return P > 0 && O > 0 && P / Aspect > 0 && O / Aspect > 0 &&
            std::isfinite(P) && std::isfinite(P / Aspect) &&
            std::isfinite(O) && std::isfinite(O / Aspect) &&
            std::isfinite(D) && std::isfinite(-Near * D) &&
            std::isfinite(OD) && std::isfinite(-Near * OD);
    }
}

bool UCameraComponent::AreParametersValid(float FOV, float Aspect,
	float Near, float Far, float Speed, float Height)
{
	// 검사 수식을 복제하지 않고 기존 구현을 그대로 재사용한다.
	return ValidCamera(FOV, Aspect, Near, Far, Speed, Height);
}

// 새 투영 값 전체를 검증한 뒤 카메라에 함께 반영한다.
bool UCameraComponent::TrySetProjection(float InFOV, float InNearZ, float InFarZ)
{
	// 실제 카메라의 화면 비율과 나머지 설정까지 포함하여 검사한다.
	if (!AreParametersValid(InFOV, AspectRatio, InNearZ,
		InFarZ, MoveSpeed, OrthoHeight))
		return false;

	// 검사를 모두 통과한 뒤에만 기존 상태를 변경한다.
	FOV = InFOV;
	NearZ = InNearZ;
	FarZ = InFarZ;
	return true;
}

// 다른 투영 값은 유지하면서 유효한 Near만 적용한다.
void UCameraComponent::SetNearZ(float InNearZ)
{
	// 실패하면 FOV·Near·Far 모두 기존 값을 유지한다.
	TrySetProjection(FOV, InNearZ, FarZ);
}

// 다른 투영 값은 유지하면서 유효한 Far만 적용한다.
void UCameraComponent::SetFarZ(float InFarZ)
{
	// 실패하면 FOV·Near·Far 모두 기존 값을 유지한다.
	TrySetProjection(FOV, NearZ, InFarZ);
}

FVector UCameraComponent::GetForward() const
{
	FVector4 ForwardVector(1.0f, 0.0f, 0.0f, 0.0f);

	return FVector(ForwardVector * GetCameraRotationMatrix());
}

FVector UCameraComponent::GetRight() const
{
	FVector4 RightVector(0.0f, 1.0f, 0.0f, 0.0f);
	return FVector(RightVector * GetCameraRotationMatrix());
}

FVector UCameraComponent::GetUp() const
{
	FVector4 UpVector(0.0f, 0.0f, 1.0f, 0.0f);
	return FVector(UpVector * GetCameraRotationMatrix());
}

void UCameraComponent::RemoveRoll()
{
    SetRelativeRotation(GetRelativeRotation().GetWithoutRoll());
}

void UCameraComponent::ConstrainEditorRotation()
{
    SetRelativeRotation(GetRelativeRotation().GetUprightCameraRotation());
}

FMatrix UCameraComponent::GetViewMatrix() const
{
	FMatrix RotationMatrix = GetCameraRotationMatrix();
	FMatrix InverseTranslationMatrix = FMatrix::MakeTranslationMatrix(RelativeLocation * -1.0f);
	return (InverseTranslationMatrix) * (RotationMatrix.Transpose());
}

FMatrix UCameraComponent::GetProjectionMatrix() const
{
	if (bIsPerspective)
	{
		return GetPerspectiveProjectionMatrix();
	}
	else
	{
		return GetOrthographicProjectionMatrix();
	}
}



float UCameraComponent::GetFOV() const
{
	return FOV;
}

float UCameraComponent::GetAspectRatio() const
{
	return AspectRatio;
}

float UCameraComponent::GetNearZ() const
{
	return NearZ;
}

float UCameraComponent::GetFarZ() const
{
	return FarZ;
}

bool UCameraComponent::GetIsPerspective() const
{
	return bIsPerspective;
}

void UCameraComponent::SetIsPerspective(bool Value)
{
	bIsPerspective = Value;
}

// 현재 클리핑 범위와 호환되는 라디안 단위 FOV만 적용한다.
void UCameraComponent::SetFOVByRadian(const float& InRadian)
{
	// FOV 변경도 동일한 투영 검증·적용 경로를 사용한다.
	TrySetProjection(InRadian, NearZ, FarZ);
}

void UCameraComponent::SetFOVByDegree(const float& InDegree)
{
	//도 단위의 각도를 라디안으로 자동으로 변환해서 세팅해줌.
	SetFOVByRadian(InDegree * (PI / 180.f));
}

void UCameraComponent::SetAspectRatio(const float& InRatio)
{
	if (ValidCamera(FOV, InRatio, NearZ, FarZ, MoveSpeed, OrthoHeight)) AspectRatio = InRatio;
}

void UCameraComponent::LookAt(const FVector& InTargetPosition)
{
	FVector Forward = (InTargetPosition - RelativeLocation);
	if (Forward.Length() <= EPSILON) {
		//카메라의 위치를 바라보는 경우
		return;
	}
    const FQuaternion Delta = FQuaternion::FromToRotation(GetForward(), Forward);
    // Align the view, then keep the horizon upright without changing the target.
    AddWorldRotation(Delta);
    RemoveRoll();

}

// 현재 카메라 상태와 함께 검증한 이동 속도만 적용한다.
void UCameraComponent::SetMoveSpeed(const float& InMoveSpeed)
{
	// 음수나 유한하지 않은 속도로 카메라 상태가 깨지는 것을 막는다.
	if (!AreParametersValid(FOV, AspectRatio, NearZ,
		FarZ, InMoveSpeed, OrthoHeight))
		return;

	MoveSpeed = InMoveSpeed;
}

float UCameraComponent::GetMoveSpeed() const
{
	return MoveSpeed;
}

void UCameraComponent::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);

	float FOVValue = FOV;
	float AspectValue = AspectRatio;
	float NearValue = NearZ;
	float FarValue = FarZ;
	float SpeedValue = MoveSpeed;
	float HeightValue = OrthoHeight;
	bool bPerspectiveValue = bIsPerspective;

	Archive.Field("FOV", FOVValue);
	Archive.Field("AspectRatio", AspectValue);
	Archive.Field("Near", NearValue);
	Archive.Field("Far", FarValue);
	Archive.Field("MoveSpeed", SpeedValue);
	Archive.Field("OrthogonalHeight", HeightValue);
	Archive.Field("Perspective", bPerspectiveValue);

	// 카메라 값 사이의 관계까지 검사한 뒤 한 번에 반영한다.
	if (Archive.IsLoading())
	{
		if (!ValidCamera(FOVValue, AspectValue, NearValue, FarValue, SpeedValue, HeightValue))
			throw std::runtime_error("Invalid camera parameters.");

		FOV = FOVValue;
		AspectRatio = AspectValue;
		NearZ = NearValue;
		FarZ = FarValue;
		MoveSpeed = SpeedValue;
		OrthoHeight = HeightValue;
		bIsPerspective = bPerspectiveValue;
	}
}

float UCameraComponent::GetOrthoHeight() const
{
	return OrthoHeight;
}

void UCameraComponent::SetOrthoHeight(float InHeight)
{
	if (!ValidCamera(FOV, AspectRatio, NearZ, FarZ, MoveSpeed, InHeight))
	{
		return;
	}
	OrthoHeight = InHeight;
}

FMatrix UCameraComponent::GetOrthographicProjectionMatrix() const
{
	assert(std::isfinite(OrthoHeight) && OrthoHeight > 0.0f);
	assert(std::isfinite(AspectRatio) && AspectRatio > 0.0f);
	assert(std::isfinite(NearZ) && std::isfinite(FarZ) && FarZ > NearZ);

	const float VerticalScale = 2.0f / OrthoHeight;
	const float HorizontalScale = VerticalScale / AspectRatio;
	const float DepthScale = 1.0f / (FarZ - NearZ);

	// +X forward, +Y right, +Z up; row vectors, depth 0..1, W = 1.
	return FMatrix(
		0.0f, 0.0f, DepthScale, 0.0f,
		HorizontalScale, 0.0f, 0.0f, 0.0f,
		0.0f, VerticalScale, 0.0f, 0.0f,
		0.0f, 0.0f, -NearZ * DepthScale, 1.0f);
}

FMatrix UCameraComponent::GetPerspectiveProjectionMatrix() const
{
	//시야각, 가로세로 비율,  가시경계 범위 체크
	assert(std::isfinite(FOV) && FOV > 0.0f && FOV < PI);
	assert(std::isfinite(AspectRatio) && AspectRatio > 0.0f);
	assert(std::isfinite(NearZ) && std::isfinite(FarZ) && NearZ > 0.0f && FarZ > NearZ);

	const float VerticalScale = 1.0f / std::tan(FOV * 0.5f);
	const float HorizontalScale = VerticalScale / AspectRatio;
	const float DepthScale = FarZ / (FarZ - NearZ);

	//일반적인 투영행렬과 다른이유 : 기준 축이 달라서 축변환 적용
	return FMatrix(
		0.0f, 0.0f, DepthScale, 1.0f,
		HorizontalScale, 0.0f, 0.0f, 0.0f,
		0.0f, VerticalScale, 0.0f, 0.0f,
		0.0f, 0.0f, -NearZ * DepthScale, 0.0f);
}

void UCameraComponent::MoveCamera(const float& InForward, const float& InRight, const float& InUp, const float& InDeltaTime)
{
	FVector InVelocity = GetForward() * InForward + GetRight() * InRight + FVector::Up*InUp;
	if (InVelocity.Length() < EPSILON) return;
	InVelocity.Normalize();
	SetRelativeLocation(RelativeLocation + InVelocity * MoveSpeed * InDeltaTime);
}

FMatrix UCameraComponent::GetCameraRotationMatrix() const
{
    return  GetRelativeRotation().ToRotationMatrix();
}
