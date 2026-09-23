#include "pch.h"
#include "Engine.h"
#include "Windows.h"

#include "Engine/Object/ObjectFactory.h"
#include "Engine/Object/ObjectStatics.h"
#include "Engine/Object/Object.h"
#include "Core/Core.h"
#include "Engine/Log.h"

#include "Editor/Editor.h"
#include "Editor/ObjViewer.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Console.h"

#include "Engine/Renderer/Device.h"
#include "Engine/Resource/ResourceManager.h"

#include <chrono>
#include <stdexcept>

#include "Editor/Window/ConsoleWindow.h"
#include "Editor/Window/EditorWindow.h"
#include "Editor/Window/PropertyWindow.h"
#include "Editor/Window/PlaceActorWindow.h"

float GEngine::GetTime()
{
	static auto Start = std::chrono::steady_clock::now();
	auto Now = std::chrono::steady_clock::now();

	return std::chrono::duration<float>(Now - Start).count();
}

GEngine* GEngine::GetInstance()
{
    static GEngine Instance{};
    return &Instance;
}

// 엔진을 초기 상태로 초기화합니다.
bool GEngine::Initialize(HWND InHwnd, EApplicationMode Mode, const std::function<bool(GResourceManager&)>& PrepareResources)
{
    ApplicationMode = Mode;
    try
    {
        // 콘솔 초기화
        Console = new FConsole();
        Console->Initialize();

        // Device 초기화
        // DirectX 백버퍼 크기를 실제 윈도우 클라이언트 크기에 맞춥니다.
        // Main() { CreateWindwoExW(... 1024,1024 ...) }  -> 제목 표시줄과 테두리를 포함한 전체 창 크기
        // GetClientRect() -> 제목 표시줄과 테두리를 제외한 클라이언트 영역
        // 이를 사용함으로 프로그램 사용 초기 Imgui출력 위치가 이상한 문제가 해결됩니다.
        RECT ClientRect{};
        if (!GetClientRect(InHwnd, &ClientRect)) throw std::runtime_error("GetClientRect failed");
        const uint32 ClientWidth = static_cast<uint32>(ClientRect.right - ClientRect.left);
        const uint32 ClientHeight = static_cast<uint32>(ClientRect.bottom - ClientRect.top);
        GDevice& Device = *GDevice::GetInstance();
        Device.Initialize();
        // 리소스 매니저 초기화
        GResourceManager& ResourceManager = *GResourceManager::GetInstance();
        ResourceManager.Initialize(&Device);

        // GPU 메시를 만들 수 있는 시점에 호출자의 준비 작업을 동기적으로 실행한다.
        if (PrepareResources && !PrepareResources(ResourceManager))
        {
            // 취소되면 지금까지 생성한 메시와 엔진 리소스를 정리한다.
            Destroy();
            return false;
        }

        /*
        * 태양계 스폰 시 사용할 텍스처 미리 불러오기
        */
        if (ApplicationMode == EApplicationMode::Editor)
        {
            ResourceManager.GetOrLoadStaticMesh("Assets/Models/Sphere.obj");

            ResourceManager.GetOrLoadTexture("Assets/Textures/sun.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/mercury.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/venus.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/earth.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/moon.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/mars.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/jupiter.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/makemake.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/ceres.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/saturn.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/uranus.png");
            ResourceManager.GetOrLoadTexture("Assets/Textures/neptune.png");
        }
        

        // 렌더러 초기화
        Renderer.Create(InHwnd, &Device, ClientWidth, ClientHeight);
        if (ApplicationMode == EApplicationMode::Editor)
        {
            //씬매니저 초기화
            GSceneManager::GetInstance()->Initialize();
            Editor = new FEditor();
        }
        else if(Mode == EApplicationMode::ObjViewer)
        {
            Editor = new FObjViewer();
        }
        
        // 가상함수로 객체별 이니셜라이즈
        Editor->Initialize();

        StartTime = GetTime();
        LastTickTime = GetTime();
        return true;
    }
    catch (...)
    {
        Destroy();
        throw;
    }
}

// 엔진의 메인 게임 루프를 실행합니다.
void GEngine::Tick()
{
    using Clock = std::chrono::high_resolution_clock;
	float DeltaTime = GetTime() - LastTickTime;
	LastTickTime = GetTime();

    auto StartGame = Clock::now();
	if (DeltaTime > 0.1f)
	{
		UE_LOG("[경고] 프레임 업데이트 시간이 100ms를 초과했습니다. 걸린 시간: {:.1f} ms", DeltaTime * 1000);
	}

    if (ApplicationMode == EApplicationMode::Editor)
    {
        GSceneManager::GetInstance()->Tick(DeltaTime);
    }

	Editor->Tick(DeltaTime);

    auto EndGame = Clock::now();
    float CurGameMs = std::chrono::duration<float, std::milli>(EndGame - StartGame).count();
    GameTimeMs = (GameTimeMs * 0.9f) + (CurGameMs * 0.1f); // 프레임은 매 프레임마다 요동치므로 지수평균으로 보간

	// 게임 화면을 렌더링합니다.
    UScene* CurrentScene = Editor->GetCurrentScene();
	Renderer.Render(DeltaTime, Editor, CurrentScene);

    EngineStats.UpdateUnitStat(DeltaTime, GameTimeMs, Renderer.GetDrawTimeMs(),
        Renderer.GetGPUTimeMs(), Renderer.GetGPUWaitMs());
    EngineStats.UpdateMemoryStat();
}

// 엔진의 자원을 정리합니다.
void GEngine::Destroy()
{
	// 에디터 정리
	if (Editor) Editor->Release();
	delete Editor;
	Editor = nullptr;

	// 씬 매니저 정리
    if (ApplicationMode == EApplicationMode::Editor)
    {
        GSceneManager::GetInstance()->Release();
    }


	// GObjectStatics 정리
	GObjectStatics::Release();

	//렌더러 해제
	Renderer.Shutdown();

	// 리소스 매니저 정리
	GResourceManager::GetInstance()->Shutdown();

	//디바이스 해제
	GDevice::GetInstance()->Release();

	// 콘솔 정리
	delete Console;
	Console = nullptr;
}
void GEngine::OnResize(uint32 Width, uint32 Height)
{
    Renderer.OnResize(Width, Height);
    if (Editor)
    {
        Editor->OnResize(Width, Height);
    }
}

const D3D11_VIEWPORT& GEngine::GetViewport() const
{
    return Renderer.GetViewport();
}