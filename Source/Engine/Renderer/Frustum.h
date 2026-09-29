#pragma once

#include "pch.h"

#include "Core/Math/Matrix.h"
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
	FPlane Left;
	FPlane Right;
	FPlane Bottom;
	FPlane Top;
	FPlane Near;
	FPlane Far;

	static FFrustum FrustumFromViewProjection(const FMatrix& M)
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

			positiveVertex.X =
				plane.A >= 0.0f ? bounds.Max.X : bounds.Min.X;

			positiveVertex.Y =
				plane.B >= 0.0f ? bounds.Max.Y : bounds.Min.Y;

			positiveVertex.Z =
				plane.C >= 0.0f ? bounds.Max.Z : bounds.Min.Z;

			// Plane 방향으로 가장 멀리 있는 점조차 바깥이면
			// AABB 전체가 Frustum 밖
			if (plane.Distance(positiveVertex) < 0.0f)
			{
				return false;
			}
		}

		return true;
	}
};
