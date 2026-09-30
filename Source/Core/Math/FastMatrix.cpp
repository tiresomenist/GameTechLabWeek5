#include "pch.h"
#include "FastMatrix.h"
#include "Core/Math/Quaternion.h"

#include <DirectXMath.h>
#include <xmmintrin.h>
#include <cassert>
#include <cmath>

#if !defined(_XM_SSE_INTRINSICS_) || defined(_XM_NO_INTRINSICS_)
#error FFastMatrix requires the DirectXMath SSE path.
#endif

using namespace DirectX;

namespace
{
    XMMATRIX LoadMatrix(const FFastMatrix& Matrix)
    {
        return XMMATRIX(
            _mm_loadu_ps(Matrix.M[0]),
            _mm_loadu_ps(Matrix.M[1]),
            _mm_loadu_ps(Matrix.M[2]),
            _mm_loadu_ps(Matrix.M[3]));
    }

    void StoreMatrix(FFastMatrix& Matrix, FXMMATRIX Value)
    {
        _mm_storeu_ps(Matrix.M[0], Value.r[0]);
        _mm_storeu_ps(Matrix.M[1], Value.r[1]);
        _mm_storeu_ps(Matrix.M[2], Value.r[2]);
        _mm_storeu_ps(Matrix.M[3], Value.r[3]);
    }

    FFastMatrix MakeMatrix(FXMMATRIX Value)
    {
        FFastMatrix Result(FFastMatrixUninitializedTag{});
        StoreMatrix(Result, Value);
        return Result;
    }

    bool TryInverseMatrix(FXMMATRIX Input, XMMATRIX& OutInverse)
    {
        if (XMMatrixIsNaN(Input) || XMMatrixIsInfinite(Input))
            return false;

        XMVECTOR Determinant;
        const XMMATRIX Candidate = XMMatrixInverse(&Determinant, Input);
        const float Det = XMVectorGetX(Determinant);

        if (!std::isfinite(Det) || Det == 0.0f ||
            XMMatrixIsNaN(Candidate) || XMMatrixIsInfinite(Candidate))
            return false;

        OutInverse = Candidate;
        return true;
    }

    bool NearZeroDot(FXMVECTOR A, FXMVECTOR B, float Epsilon)
    {
        return std::fabs(XMVectorGetX(XMVector4Dot(A, B))) <= Epsilon;
    }

    bool IsOrthogonalMatrix(FXMMATRIX Matrix, float Epsilon)
    {
        return NearZeroDot(Matrix.r[0], Matrix.r[1], Epsilon) &&
            NearZeroDot(Matrix.r[0], Matrix.r[2], Epsilon) &&
            NearZeroDot(Matrix.r[0], Matrix.r[3], Epsilon) &&
            NearZeroDot(Matrix.r[1], Matrix.r[2], Epsilon) &&
            NearZeroDot(Matrix.r[1], Matrix.r[3], Epsilon) &&
            NearZeroDot(Matrix.r[2], Matrix.r[3], Epsilon);
    }
}

const FFastMatrix FFastMatrix::Identity{};

FFastMatrix::FFastMatrix(FFastMatrixUninitializedTag) noexcept
{
}

FFastMatrix::FFastMatrix(float m00, float m01, float m02, float m03,
    float m10, float m11, float m12, float m13,
    float m20, float m21, float m22, float m23,
    float m30, float m31, float m32, float m33)
    : M{
        {m00, m01, m02, m03},
        {m10, m11, m12, m13},
        {m20, m21, m22, m23},
        {m30, m31, m32, m33}
    }
{
}

FFastMatrix FFastMatrix::MakeTranslationMatrix(const FVector& Location)
{
    return MakeMatrix(XMMatrixTranslation(
        Location.X, Location.Y, Location.Z));
}

FFastMatrix FFastMatrix::MakeScaleMatrix(const FVector& Scale)
{
    return MakeMatrix(XMMatrixScaling(
        Scale.X, Scale.Y, Scale.Z));
}

FFastMatrix FFastMatrix::MakeRotationXMatrix(float Radian)
{
    // Todo: No need exception
    if (!std::isfinite(Radian))
        return Identity;

    return MakeMatrix(XMMatrixRotationX(Radian));
}

FFastMatrix FFastMatrix::MakeRotationYMatrix(float Radian)
{
    if (!std::isfinite(Radian))
        return Identity;

    return MakeMatrix(XMMatrixRotationY(Radian));
}

FFastMatrix FFastMatrix::MakeRotationZMatrix(float Radian)
{
    if (!std::isfinite(Radian))
        return Identity;

    return MakeMatrix(XMMatrixRotationZ(Radian));
}

FFastMatrix FFastMatrix::MakeRotationMatrix(const FVector& Rotation)
{
    // 기존 FMatrix와 동일한 Euler 해석을 사용한다.
    const FQuaternion Q = FQuaternion::FromEuler(Rotation);
    return MakeMatrix(XMMatrixRotationQuaternion(
        XMVectorSet(Q.X, Q.Y, Q.Z, Q.W)));
}

FFastMatrix FFastMatrix::MakeRotationMatrix(const FQuaternion& Rotation)
{
    FQuaternion Q = Rotation;
    Q.Normalize();

    return MakeMatrix(XMMatrixRotationQuaternion(
        XMVectorSet(Q.X, Q.Y, Q.Z, Q.W)));
}

FFastMatrix FFastMatrix::MakeModelMatrix(
    const FVector& Location,
    const FQuaternion& Rotation,
    const FVector& Scale)
{
    // 기존 FQuaternion::ToRotationMatrix와 같이 정규화한다.
    FQuaternion Q = Rotation;
    Q.Normalize();

    const XMMATRIX Scaling = XMMatrixScaling(
        Scale.X, Scale.Y, Scale.Z);
    const XMMATRIX Rotating = XMMatrixRotationQuaternion(
        XMVectorSet(Q.X, Q.Y, Q.Z, Q.W));
    const XMMATRIX Translating = XMMatrixTranslation(
        Location.X, Location.Y, Location.Z);

    return MakeMatrix(XMMatrixMultiply(
        XMMatrixMultiply(Scaling, Rotating), Translating));
}

FFastMatrix FFastMatrix::MakeNormalMatrix(const FFastMatrix& Model)
{
    return Model.NormalMatrix();
}

FFastMatrix FFastMatrix::operator*(const FFastMatrix& Rhs) const
{
    return MakeMatrix(XMMatrixMultiply(
        LoadMatrix(*this), LoadMatrix(Rhs)));
}

FVector4 FFastMatrix::TransformVector4(const FVector4& V) const
{
    const XMVECTOR Result = XMVector4Transform(
        XMVectorSet(V.X, V.Y, V.Z, V.W),
        LoadMatrix(*this));

    return FVector4(
        XMVectorGetX(Result),
        XMVectorGetY(Result),
        XMVectorGetZ(Result),
        XMVectorGetW(Result));
}

FVector FFastMatrix::TransformPosition(const FVector& V) const
{
    // 기존 TransformPosition과 같이 원근 나눗셈을 하지 않는다.
    const XMVECTOR Result = XMVector3Transform(
        XMVectorSet(V.X, V.Y, V.Z, 0.0f),
        LoadMatrix(*this));

    return FVector(
        XMVectorGetX(Result),
        XMVectorGetY(Result),
        XMVectorGetZ(Result));
}

FFastMatrix FFastMatrix::Transpose() const
{
    return MakeMatrix(XMMatrixTranspose(LoadMatrix(*this)));
}

bool FFastMatrix::TryInverse(FFastMatrix& OutInverse) const
{
    XMMATRIX Inverse;
    if (!TryInverseMatrix(LoadMatrix(*this), Inverse))
        return false;

    StoreMatrix(OutInverse, Inverse);
    return true;
}

FFastMatrix FFastMatrix::Inverse() const
{
    XMMATRIX Inverse;
    const bool Success = TryInverseMatrix(LoadMatrix(*this), Inverse);
    assert(Success && "Matrix is singular.");

    return Success ? MakeMatrix(Inverse) : Identity;
}

float FFastMatrix::Determinant() const
{
    return XMVectorGetX(XMMatrixDeterminant(LoadMatrix(*this)));
}

FVector FFastMatrix::GetAxis(int32 AxisIndex) const
{
    assert(AxisIndex >= 0 && AxisIndex < 3);
    return FVector(
        M[AxisIndex][0],
        M[AxisIndex][1],
        M[AxisIndex][2]);
}

FVector FFastMatrix::GetOrigin() const
{
    return FVector(M[3][0], M[3][1], M[3][2]);
}

FFastMatrix FFastMatrix::NormalMatrix() const
{
    XMMATRIX Matrix = LoadMatrix(*this);
    Matrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    XMMATRIX Inverse;
    const bool Success = TryInverseMatrix(Matrix, Inverse);
    assert(Success && "Matrix is singular.");

    return Success
        ? MakeMatrix(XMMatrixTranspose(Inverse))
        : Identity;
}

bool FFastMatrix::IsOrthogonal(float Epsilon) const
{
    return IsOrthogonalMatrix(LoadMatrix(*this), Epsilon);
}

bool FFastMatrix::IsOrthonormal(float Epsilon) const
{
    const XMMATRIX Matrix = LoadMatrix(*this);
    if (!IsOrthogonalMatrix(Matrix, Epsilon))
        return false;

    for (int Row = 0; Row < 4; ++Row)
    {
        const float Length =
            XMVectorGetX(XMVector4Length(Matrix.r[Row]));
        if (std::fabs(Length - 1.0f) > Epsilon)
            return false;
    }

    return true;
}

FVector4 operator*(const FVector4& V, const FFastMatrix& Matrix)
{
    return Matrix.TransformVector4(V);
}
