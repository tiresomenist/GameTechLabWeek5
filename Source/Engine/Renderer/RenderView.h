#pragma once

#include <d3d11.h>
#include "Engine/Renderer/ViewSettings.h"

class UCameraComponent;

// 기존 enum 값과 순서를 유지한다.
enum class EViewportType
{
    Perspective,
    Top,
    Front,
    Right
};

struct FRenderView
{
    UCameraComponent* Camera = nullptr;

    D3D11_VIEWPORT Viewport{};
    FViewSettings ViewSettings{};

    bool bDrawEditorGizmos = false;
    EViewportType ViewType = EViewportType::Perspective;
};