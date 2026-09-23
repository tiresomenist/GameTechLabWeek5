#include "pch.h"
#include <windows.h>

// ImGui 관련 헤더 삽입
#include "ImGui/imgui.h"

//렌더러 헤더파일
#include "Engine/Engine.h"
#include "Engine/Input/WndProc.h"
#include "Engine/Log.h"

#include "resource.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <exception>
#include "LoadingScreen.h"
#include <stdexcept>
#include "Engine/Resource/ResourceManager.h"
#include <utility>

namespace
{
    // 본창을 소유한 메인 스레드에서만 접근하며 엔진 메시지 처리 가능 여부를 나타낸다.
    bool bEngineReady = false;

    // 바이너리 메시를 프리로드하고 진행 상황과 취소를 로딩 창에 연결한다.
    bool PrepareStartupResources(GResourceManager& ResourceManager,
        FLoadingScreen& LoadingScreen)
    {
        // 기본 리소스 초기화 중 들어온 취소는 캐시 검색 전에 처리한다.
        if (LoadingScreen.IsCancelRequested()) return false;

        // 리소스 매니저의 진행 정보를 로딩 창이 사용하는 표시 상태로 변환한다.
        const auto OnProgress = [&LoadingScreen](const FStaticMeshPreloadResult& Progress)
            {
                FLoadingScreenStatus Status;
                Status.Stage = Progress.bSearchComplete
                    ? L"Loading Cache..."
                    : L"Searching Cache...";
                if (Progress.bCancelled)
                    Status.Stage = L"Cancel Launch Engine ...";

                Status.FileName = Progress.CurrentFile.filename().native();
                Status.TotalCount = Progress.TotalCount;
                Status.LoadedCount = Progress.LoadedCount;
                Status.SkippedCount = Progress.SkippedCount;
                Status.FailedCount = Progress.FailedCount;
                Status.bDeterminate = Progress.bSearchComplete;
                LoadingScreen.SetStatus(std::move(Status));
            };

        // 로딩 창의 취소 요청은 리소스 매니저가 안전한 작업 경계에서 조회한다.
        const auto ShouldCancel = [&LoadingScreen]() -> bool
            {
                return LoadingScreen.IsCancelRequested();
            };

        // 정해진 모델 폴더의 바이너리만 준비하며 실제 작업은 메인 스레드에서 실행한다.
        const FStaticMeshPreloadResult Result =
            ResourceManager.PreloadCachedStaticMeshes(
                L"Assets/Models", OnProgress, ShouldCancel);

        // 검색 오류도 최종 로그에 남겨 일부 폴더가 누락된 경우를 확인할 수 있게 한다.
        UE_LOG("[MeshPreload] Total={} Loaded={} Skipped={} Failed={} SearchErrors={} Cancelled={}",
            Result.TotalCount, Result.LoadedCount, Result.SkippedCount,
            Result.FailedCount, Result.SearchErrorCount, Result.bCancelled);

        if (Result.bCancelled || LoadingScreen.IsCancelRequested())
            return false;

        // 프리로드 완료 후에도 렌더러와 에디터 초기화가 남아 있으므로 준비 중 표시를 유지한다.
        FLoadingScreenStatus Status;
        Status.Stage = L"Wait a Second...";
        LoadingScreen.SetStatus(std::move(Status));

        // 상태를 갱신하는 사이 들어온 취소도 엔진 초기화에 전달한다.
        return !LoadingScreen.IsCancelRequested();
    }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 각종 메시지를 처리할 함수
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 본창 종료는 초기화 여부와 관계없이 메인 루프에 전달한다.
    if (message == WM_DESTROY)
    {
        bEngineReady = false;
        PostQuitMessage(0);
        return 0;
    }

    // 창 생성 중에는 렌더러와 ImGui가 준비되지 않았으므로 기본 처리만 수행한다.
    if (!bEngineReady)
    {
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    // ImGUI 메세지는 ImGUI가 처리
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    {
        return true;
    }

    switch (message)
    {
    case WM_SIZE: //lParam -> (Width|Height)=(LO|HI)
        {
        // 창 최소화 시 Width, Height가 0이므로 리사이즈하지 않음
        if (wParam == SIZE_MINIMIZED)
        {
            GEngine::GetInstance()->OnResize(0, 0);
            return 0;
        }

        const uint32 Width = static_cast<uint32>(LOWORD(lParam));
        const uint32 Height = static_cast<uint32>(HIWORD(lParam));

        try { GEngine::GetInstance()->OnResize(Width, Height); }
        catch (...) { PostQuitMessage(EXIT_FAILURE); }

        return 0;
        }
        break;
    default:
        return HandleInput(hWnd, message, wParam, lParam);
    }

    return 0;
}

// 로딩 창을 표시한 상태로 엔진을 초기화하고 준비가 끝나면 본창을 실행한다.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
#if defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    constexpr wchar_t WindowClassName[] = L"JungleWindowClass";
#if defined(OBJVIEWER_APP)
    constexpr EApplicationMode ApplicationMode = EApplicationMode::ObjViewer;
    constexpr wchar_t Title[] = L"PEPE Viewer";
#else
    constexpr EApplicationMode ApplicationMode = EApplicationMode::Editor;
    constexpr wchar_t Title[] = L"PEPE Engine";
#endif

    // 기존 본창 클래스를 등록하되 아직 실제 창은 생성하지 않는다.
    WNDCLASSW WindowClass{};
    WindowClass.lpfnWndProc = WndProc;
    WindowClass.hInstance = hInstance;
    WindowClass.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    WindowClass.lpszClassName = WindowClassName;
    if (!RegisterClassW(&WindowClass))
    {
        MessageBoxW(nullptr, L"프로그램 창 클래스를 등록하지 못했습니다.", Title, MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }

    GEngine* Engine = GEngine::GetInstance();
    FLoadingScreen LoadingScreen;
    HWND hWnd = nullptr;
    bool bEngineInitialized = false;
    int ExitCode = EXIT_SUCCESS;
    bEngineReady = false;

    try
    {
        // 엔진 초기화 전에 별도 UI 스레드의 로고 창을 준비한다.
        LoadingScreen.Start(hInstance, Title);

        // 취소 시 break로 빠져나와 함수 아래의 공통 정리를 수행한다.
        do
        {
            if (LoadingScreen.IsCancelRequested()) break;

            // WS_VISIBLE을 제외하여 초기화 중 빈 본창이 표시되지 않게 한다.
            constexpr int WindowWidth = 1600;
            constexpr int WindowHeight = 1024;
            hWnd = CreateWindowExW(
                0, WindowClassName, Title, WS_POPUP | WS_OVERLAPPEDWINDOW,
                (GetSystemMetrics(SM_CXSCREEN) - WindowWidth) / 2,
                (GetSystemMetrics(SM_CYSCREEN) - WindowHeight) / 2,
                WindowWidth, WindowHeight, nullptr, nullptr, hInstance, nullptr);
            if (!hWnd)
                throw std::runtime_error("Failed to create main application window.");

            if (LoadingScreen.IsCancelRequested()) break;

            // 엔진 작업은 기존 메인 스레드에서 실행하고 로딩 창에는 표시 정보만 전달한다.
            FLoadingScreenStatus Status;
            Status.Stage = L"Initialize Engine...";
            LoadingScreen.SetStatus(std::move(Status));
            // 엔진이 리소스 매니저를 준비한 시점에 프리로드와 로딩 화면 갱신을 실행한다.
            if (!Engine->Initialize(hWnd, ApplicationMode,
                [&LoadingScreen](GResourceManager& ResourceManager) -> bool
                {
                    // 실행 프로그램에 관계없이 같은 시작 리소스 준비 함수를 사용한다.
                    return PrepareStartupResources(ResourceManager, LoadingScreen);
                }))
            {
                // 취소된 초기화는 엔진 내부에서 정리되었으므로 공통 종료로 이동한다.
                break;
            }
            // 전체 초기화가 성공한 경우에만 종료 시 엔진 정리를 호출한다.
            bEngineInitialized = true;

            // 초기화 중 요청된 취소는 초기화가 반환된 안전한 시점에 처리한다.
            if (LoadingScreen.IsCancelRequested()) break;

            // 초기화 전에 무시했던 크기 메시지를 대신하여 현재 클라이언트 크기를 반영한다.
            RECT ClientRect{};
            if (!GetClientRect(hWnd, &ClientRect))
                throw std::runtime_error("Failed to read main window client size.");
            Engine->OnResize(
                static_cast<uint32>(ClientRect.right - ClientRect.left),
                static_cast<uint32>(ClientRect.bottom - ClientRect.top));

            // 로딩 UI의 종료까지 기다린 뒤 최종 취소 상태를 확인한다.
            LoadingScreen.Stop();
            if (LoadingScreen.IsCancelRequested()) break;

            // 엔진 준비 후에만 본창 메시지 처리를 허용하고 화면을 표시한다.
            bEngineReady = true;
            ShowWindow(hWnd, nShowCmd);
            UpdateWindow(hWnd);
            SetForegroundWindow(hWnd);

            bool bIsExit = false;
            while (!bIsExit)
            {
                // 종료 메시지를 먼저 확인하여 종료된 창에 다음 프레임을 그리지 않는다.
                MSG Message{};
                while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (Message.message == WM_QUIT)
                    {
                        ExitCode = static_cast<int>(Message.wParam);
                        bIsExit = true;
                        break;
                    }
                    TranslateMessage(&Message);
                    DispatchMessageW(&Message);
                }

                if (bIsExit) break;
                Engine->Tick();
            }
        } while (false);
    }
    catch (const std::exception& Error)
    {
        // 오류 안내 중에는 엔진 메시지 처리를 차단하고 로딩 UI부터 종료한다.
        bEngineReady = false;
        LoadingScreen.Stop();
        OutputDebugStringA(Error.what());
        MessageBoxA(nullptr, Error.what(), "PEPE Application Error", MB_OK | MB_ICONERROR);
        ExitCode = EXIT_FAILURE;
    }
    catch (...)
    {
        // 형식을 알 수 없는 예외도 창과 스레드의 공통 정리 경로로 보낸다.
        bEngineReady = false;
        LoadingScreen.Stop();
        MessageBoxW(nullptr, L"프로그램 실행 중 오류가 발생했습니다.", Title, MB_OK | MB_ICONERROR);
        ExitCode = EXIT_FAILURE;
    }

    // 성공·취소·오류 모두 UI 스레드가 종료된 뒤 엔진과 본창을 정리한다.
    bEngineReady = false;
    LoadingScreen.Stop();
    if (bEngineInitialized) Engine->Destroy();
    if (hWnd && IsWindow(hWnd)) DestroyWindow(hWnd);
    UnregisterClassW(WindowClassName, hInstance);
    return ExitCode;
}
