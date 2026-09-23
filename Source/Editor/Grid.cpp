#include "pch.h"
#include "Grid.h"
#include "Engine/Object/Object.h"
#include "Engine/Object/ClassType.h"
#include "Engine/Renderer/VertexSimple.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/MeshNames.h"

void UGrid::Initialize(FEditor* InEditor)
{
	Editor = InEditor;
	MeshResource = GResourceManager::GetInstance()->GetPrimitive(GetMeshNames().Grid);
}

TArray<FPrimitiveRenderData> UGrid::GetRenderData()
{
	return TArray<FPrimitiveRenderData>();
}

FLineDrawRequest UGrid::BuildLineDrawRequest(const FGrid& Grid, const FVector& CameraPosition, EViewportType InViewType) const
{
    FLineDrawRequest Request;

    if (!std::isfinite(Grid.Interval) || Grid.Interval < FGrid::MinInterval || !std::isfinite(Grid.Extent) ||
        Grid.Extent <= 0.0f || !std::isfinite(CameraPosition.X) || !std::isfinite(CameraPosition.Y) || !std::isfinite(CameraPosition.Z))
    {
        return Request;
    }

    const double Interval = Grid.Interval;
    const double Half = Grid.Extent;

    const double CenterX = std::floor(CameraPosition.X / Interval) * Interval;
    const double CenterY = std::floor(CameraPosition.Y / Interval) * Interval;
    const double CenterZ = std::floor(CameraPosition.Z / Interval) * Interval;

    const double FirstX = std::ceil((CenterX - Half) / Interval);
    const double FirstY = std::ceil((CenterY - Half) / Interval);
    const double FirstZ = std::ceil((CenterZ - Half) / Interval);

    const double CountX = std::floor((CenterX + Half) / Interval) - FirstX + 1.0;
    const double CountY = std::floor((CenterY + Half) / Interval) - FirstY + 1.0;
    const double CountZ = std::floor((CenterZ + Half) / Interval) - FirstZ + 1.0;

    const double MaxVertices = static_cast<double>((std::numeric_limits<UINT>::max)()) / sizeof(FVertexSimple);

    double CenterA{}, CenterB{};
    double FirstA{}, FirstB{};
    double CountA{}, CountB{};

    switch (InViewType)
    {
    case EViewportType::Perspective:   
    case EViewportType::Top:
    {
        CenterA = CenterX;
        CenterB = CenterY;

        FirstA = FirstX;
        FirstB = FirstY;

        CountA = CountX;
        CountB = CountY;
        break;
    }
    case EViewportType::Front:
    {
        CenterA = CenterY;
        CenterB = CenterZ;

        FirstA = FirstY;
        FirstB = FirstZ;

        CountA = CountY;
        CountB = CountZ;
        break;

    }
    case EViewportType::Right:
    {
        CenterA = CenterX;
        CenterB = CenterZ;

        FirstA = FirstX;
        FirstB = FirstZ;

        CountA = CountX;
        CountB = CountZ;
        break;
    }
    }


    if (!std::isfinite(CountA) || !std::isfinite(CountB) || CountA < 0.0 || CountB < 0.0 ||
        4.0 * (CountA + CountB) > MaxVertices)
    {
        return Request;
    }

    const auto GetAlpha = [Half](double Distance)
        {
            const double Start = Half * 0.7;

            const double T = std::clamp((Distance - Start) / (Half - Start), 0.0, 1.0);

            return static_cast<float>(1.0 - T * T * (3.0 - 2.0 * T));
        };

    // 그리드가 자신의 선 연결 관계를 생성함
    const auto AddSegment = [&Request, InViewType](double AX, double AY, float AlphaA, double BX, double BY, float AlphaB)
        {
            const uint32 Base =
                static_cast<uint32>(Request.Vertices.Num());

            FVector PosA, PosB;

            switch (InViewType)
            {
            case EViewportType::Perspective:
            case EViewportType::Top:
            {
                PosA = FVector(static_cast<float>(AX), static_cast<float>(AY), 0.0f);
                PosB = FVector(static_cast<float>(BX), static_cast<float>(BY), 0.0f);
                break;
            }
            case EViewportType::Front:
            {
                PosA = FVector(0.0f, static_cast<float>(AX), static_cast<float>(AY));
                PosB = FVector(0.0f, static_cast<float>(BX), static_cast<float>(BY));
                break;
            }
            case EViewportType::Right:
            {
                PosA = FVector(static_cast<float>(AX), 0.0f, static_cast<float>(AY));
                PosB = FVector(static_cast<float>(BX), 0.0f, static_cast<float>(BY));
                break;
            }
            }

            Request.Vertices.Add({PosA.X, PosA.Y, PosA.Z,1.0f, 1.0f, 1.0f, AlphaA});

            Request.Vertices.Add({PosB.X, PosB.Y, PosB.Z,1.0f, 1.0f, 1.0f, AlphaB});

            Request.Indices.Add(Base);
            Request.Indices.Add(Base + 1);
        };

    const double AxisTolerance = Interval * 0.01;

    for (uint32 I = 0; I < static_cast<uint32>(CountA); ++I)
    {
        const double X = (FirstA + I) * Interval;

        if (std::abs(X) <= AxisTolerance)
        {
            continue;
        }

        const double Offset = X - CenterA;
        const float EndAlpha = GetAlpha(std::hypot(Offset, Half));
        const float CenterAlpha = GetAlpha(std::abs(Offset));

        AddSegment(X, CenterB - Half, EndAlpha, X, CenterB, CenterAlpha);

        AddSegment(X, CenterB, CenterAlpha, X, CenterB + Half, EndAlpha);
    }

    for (uint32 I = 0; I < static_cast<uint32>(CountB); ++I)
    {
        const double Y = (FirstB + I) * Interval;

        if (std::abs(Y) <= AxisTolerance)
        {
            continue;
        }

        const double Offset = Y - CenterB;
        const float EndAlpha = GetAlpha(std::hypot(Offset, Half));
        const float CenterAlpha = GetAlpha(std::abs(Offset));

        AddSegment(CenterA - Half, Y, EndAlpha,CenterA, Y, CenterAlpha);

        AddSegment(CenterA, Y, CenterAlpha, CenterA + Half, Y, EndAlpha);
    }

    return Request;
}
