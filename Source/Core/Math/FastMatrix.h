#pragma once

#include "Core/Math/Vector.h"

struct FQuaternion;

// SIMD 결과를 곧바로 M에 저장할 때 단위행렬 초기화를 생략한다.
struct FFastMatrixUninitializedTag {};

struct FFastMatrix
{
    float M[4][4];

    static const FFastMatrix Identity;

    static FFastMatrix MakeTranslationMatrix(const FVector& Location);
    static FFastMatrix MakeScaleMatrix(const FVector& Scale);
    static FFastMatrix MakeRotationXMatrix(float Radian);
    static FFastMatrix MakeRotationYMatrix(float Radian);
    static FFastMatrix MakeRotationZMatrix(float Radian);
    static FFastMatrix MakeRotationMatrix(const FVector& Rotation);
    static FFastMatrix MakeRotationMatrix(const FQuaternion& Rotation);
    static FFastMatrix MakeModelMatrix(
        const FVector& Location,
        const FQuaternion& Rotation,
        const FVector& Scale);
    static FFastMatrix MakeNormalMatrix(const FFastMatrix& Model);

    FFastMatrix operator*(const FFastMatrix& Rhs) const;
    FVector4 TransformVector4(const FVector4& V) const;
    FVector TransformPosition(const FVector& V) const;

    FFastMatrix Transpose() const;
    bool TryInverse(FFastMatrix& OutInverse) const;
    FFastMatrix Inverse() const;
    float Determinant() const;

    FVector GetAxis(int32 AxisIndex) const;
    FVector GetOrigin() const;
    FFastMatrix NormalMatrix() const;
    bool IsOrthogonal(float Epsilon = EPSILON) const;
    bool IsOrthonormal(float Epsilon = EPSILON) const;

    FFastMatrix(float m00 = 1.0f, float m01 = 0.0f, float m02 = 0.0f, float m03 = 0.0f,
        float m10 = 0.0f, float m11 = 1.0f, float m12 = 0.0f, float m13 = 0.0f,
        float m20 = 0.0f, float m21 = 0.0f, float m22 = 1.0f, float m23 = 0.0f,
        float m30 = 0.0f, float m31 = 0.0f, float m32 = 0.0f, float m33 = 1.0f);

    explicit FFastMatrix(FFastMatrixUninitializedTag) noexcept;
};

FVector4 operator*(const FVector4& V, const FFastMatrix& Matrix);
