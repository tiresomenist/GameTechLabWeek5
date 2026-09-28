#pragma once

#include "Matrix.h"

struct FBoundingBox
{
    FVector Min{};
    FVector Max{};

    FBoundingBox() = default;


    FBoundingBox(const FVector& InMin, const FVector& InMax)
        : Min(InMin), Max(InMax)
    {
    }

    /*FBoundingBox TransformBounds(const FMatrix& Matrix) const
    {
        FBoundingBox Result;

        for (int32 Corner = 0; Corner < 8; ++Corner)
        {
            const FVector LocalPoint(
                (Corner & 1) ? Max.X : Min.X,
                (Corner & 2) ? Max.Y : Min.Y,
                (Corner & 4) ? Max.Z : Min.Z);

            const FVector WorldPoint = Matrix.TransformPosition(LocalPoint);

            if (Corner == 0)
            {
                Result.Min = WorldPoint;
                Result.Max = WorldPoint;
                continue;
            }

            Result.Min.X = std::min(Result.Min.X, WorldPoint.X);
            Result.Min.Y = std::min(Result.Min.Y, WorldPoint.Y);
            Result.Min.Z = std::min(Result.Min.Z, WorldPoint.Z);
            Result.Max.X = std::max(Result.Max.X, WorldPoint.X);
            Result.Max.Y = std::max(Result.Max.Y, WorldPoint.Y);
            Result.Max.Z = std::max(Result.Max.Z, WorldPoint.Z);
        }

        return Result;
    }*/

    FBoundingBox TransformBounds(const FMatrix& World) const
    {
        const FVector LocalCenter = (Min + Max) * 0.5f;
        const FVector LocalExtent = (Max - Min) * 0.5f;
        const FVector WorldCenter = World.TransformPosition(LocalCenter);

        // 회전/비균등 스케일을 포함한 World AABB extent
        const FVector WorldExtent(
            std::fabs(World.M[0][0]) * LocalExtent.X +
            std::fabs(World.M[1][0]) * LocalExtent.Y +
            std::fabs(World.M[2][0]) * LocalExtent.Z,

            std::fabs(World.M[0][1]) * LocalExtent.X +
            std::fabs(World.M[1][1]) * LocalExtent.Y +
            std::fabs(World.M[2][1]) * LocalExtent.Z,

            std::fabs(World.M[0][2]) * LocalExtent.X +
            std::fabs(World.M[1][2]) * LocalExtent.Y +
            std::fabs(World.M[2][2]) * LocalExtent.Z
        );

        return { WorldCenter - WorldExtent, WorldCenter + WorldExtent };
    }
};
