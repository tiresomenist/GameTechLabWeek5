#pragma once

class UCameraComponent;
enum class EViewportType;
class FViewportClient;

// Scene owns the camera; this controller only updates the assigned camera.
class FCameraController
{
public:
    void SetCamera(UCameraComponent* InCamera);
    void Tick(float DeltaTime);

    void SetViewportClient(FViewportClient* InVC);

    float RotationSensitivity = 0.5f; // Degrees per pixel.

private:
    UCameraComponent* Camera = nullptr;
    EViewportType ViewType;
    FViewportClient* ViewportClient = nullptr;
};
