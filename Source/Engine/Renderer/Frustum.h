#pragma once

#include "pch.h"

#include "Core/Math/Matrix.h"
#include "Core/Math/FastMatrix.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Box.h"


struct FPlane
{
	float A = 0.0f;
	float B = 0.0f;
	float C = 0.0f;
	float D = 0.0f;

	float Distance(const FVector& point) const
	{
		return A * point.X + B * point.Y + C * point.Z + D;
	}
};


struct FFrustum
{
	enum class EBoxResult : std::uint8_t { Outside, Intersecting, Inside };
	FPlane Left;
	FPlane Right;
	FPlane Bottom;
	FPlane Top;
	FPlane Near;
	FPlane Far;

	static FFrustum FrustumFromViewProjection(const FFastMatrix& M)
	{
		FFrustum result;

		// Left: x + w >= 0
		result.Left =
		{
			M.M[0][0] + M.M[0][3],
			M.M[1][0] + M.M[1][3],
			M.M[2][0] + M.M[2][3],
			M.M[3][0] + M.M[3][3]
		};

		// Right: w - x >= 0
		result.Right =
		{
			M.M[0][3] - M.M[0][0],
			M.M[1][3] - M.M[1][0],
			M.M[2][3] - M.M[2][0],
			M.M[3][3] - M.M[3][0]
		};

		// Bottom: y + w >= 0
		result.Bottom =
		{
			M.M[0][1] + M.M[0][3],
			M.M[1][1] + M.M[1][3],
			M.M[2][1] + M.M[2][3],
			M.M[3][1] + M.M[3][3]
		};

		// Top: w - y >= 0
		result.Top =
		{
			M.M[0][3] - M.M[0][1],
			M.M[1][3] - M.M[1][1],
			M.M[2][3] - M.M[2][1],
			M.M[3][3] - M.M[3][1]
		};

		// Near: z >= 0
		result.Near =
		{
			M.M[0][2],
			M.M[1][2],
			M.M[2][2],
			M.M[3][2]
		};

		// Far: w - z >= 0
		result.Far =
		{
			M.M[0][3] - M.M[0][2],
			M.M[1][3] - M.M[1][2],
			M.M[2][3] - M.M[2][2],
			M.M[3][3] - M.M[3][2]
		};

		return result;
	}

	bool Intersects(const FBoundingBox& bounds) const
	{
		const FPlane planes[6] =
		{
			Left,
			Right,
			Bottom,
			Top,
			Near,
			Far
		};

		for (const FPlane& plane : planes)
		{
			// Plane normal 방향으로 가장 멀리 있는 AABB 정점
			FVector positiveVertex;
			positiveVertex.X = plane.A >= 0.0f ? bounds.Max.X : bounds.Min.X;
			positiveVertex.Y = plane.B >= 0.0f ? bounds.Max.Y : bounds.Min.Y;
			positiveVertex.Z = plane.C >= 0.0f ? bounds.Max.Z : bounds.Min.Z;

			// Plane 방향으로 가장 멀리 있는 점조차 바깥이면 AABB 전체가 Frustum 밖
			if (plane.Distance(positiveVertex) < 0.0f)
			{
				return false;
			}
		}

		return true;
	}

	EBoxResult Classify(const FBoundingBox& Bounds) const
	{
		const FPlane Planes[6] = { Left, Right, Bottom, Top, Near, Far };
		bool bFullyInside = true;
		for (const FPlane& Plane : Planes)
		{
			const FVector Positive(
				Plane.A >= 0.0f ? Bounds.Max.X : Bounds.Min.X,
				Plane.B >= 0.0f ? Bounds.Max.Y : Bounds.Min.Y,
				Plane.C >= 0.0f ? Bounds.Max.Z : Bounds.Min.Z);
			if (Plane.Distance(Positive) < 0.0f) return EBoxResult::Outside;
			const FVector Negative(
				Plane.A >= 0.0f ? Bounds.Min.X : Bounds.Max.X,
				Plane.B >= 0.0f ? Bounds.Min.Y : Bounds.Max.Y,
				Plane.C >= 0.0f ? Bounds.Min.Z : Bounds.Max.Z);
			bFullyInside &= Plane.Distance(Negative) >= 1.0e-4f;
		}
		return bFullyInside ? EBoxResult::Inside : EBoxResult::Intersecting;
	}
};
