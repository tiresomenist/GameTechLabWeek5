#include "pch.h"
#include "Window.h"

bool SWindow::IsHover(const FPoint& Coord) const
{

    return Rect.Contains(Coord);
}

void SWindow::UpdateLayout(const FRect& InRect)
{
    Rect = InRect;
    if (ViewportClient)
    {
        ViewportClient->SetRect(Rect.X, Rect.Y, Rect.Width, Rect.Height);
    }
}

void SWindow::SetViewportClient(FViewportClient* InViewportClient)
{
    ViewportClient = InViewportClient;
}
