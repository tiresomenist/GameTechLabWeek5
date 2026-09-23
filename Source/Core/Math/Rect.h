#pragma once
#include "Point.h"

struct FRect
{
	float X = 0.0f;
	float Y = 0.0f;
	float Width = 0.0f;
	float Height = 0.0f;

	float Left() const { return X; }
	float Right() const { return X + Width; }
	float Top() const { return Y; }
	float Bottom() const { return Y + Height; }

	bool Contains(const FPoint& InPoint) const
	{
		return (InPoint.X >= X && InPoint.X <= Right() && InPoint.Y >= Y && InPoint.Y <= Bottom());
	}
};