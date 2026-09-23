#pragma once
#pragma once
#include "Core/Math/Rect.h"
#include "Core/Math/Point.h"
#include "Engine/Renderer/ViewportClient.h"

class SWindow
{
public:
	virtual ~SWindow() = default;

	bool IsHover(const FPoint& coord) const;
	virtual void UpdateLayout(const FRect& InRect);

	void SetViewportClient(FViewportClient* InViewportClient);

	virtual bool OnMouseDown(const FPoint& InPoint) { return false; }
	virtual bool OnMouseUp(const FPoint& InPoint) { return false; }
	virtual bool OnMouseMove(const FPoint& InPoint) { return false; }
	
	virtual void Render() {};
protected:
	FRect Rect;
	FViewportClient* ViewportClient = nullptr;

	FViewportClient* MaximizeWindow = nullptr;
};

