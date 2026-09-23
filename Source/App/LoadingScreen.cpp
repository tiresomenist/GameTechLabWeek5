#include "pch.h"
#include "LoadingScreen.h"
#include "resource.h"
#include <exception>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <format>

// 소멸 전에 실행 중인 UI 스레드를 정리한다.
FLoadingScreen::~FLoadingScreen()
{
    Stop();
}

// UI 스레드를 시작하고 로고 창이 준비될 때까지 기다린다.
void FLoadingScreen::Start(HINSTANCE Instance, FWideStringView Title)
{
    if (UIThread.joinable())
        throw std::logic_error("Loading screen is already running.");

    // 스레드 시작 전에 공유할 초기 상태와 종료 이벤트를 준비한다.
    ApplicationTitle = FWideString(Title);
    bCancelRequested.store(false);
    SetStatus(FLoadingScreenStatus{});

    StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!StopEvent)
        throw std::runtime_error("Failed to create loading screen stop event.");

    try
    {
        // 성공과 예외를 모두 전달받아 창 생성 실패 시 대기가 끝나도록 한다.
        std::promise<void> Startup;
        std::future<void> Ready = Startup.get_future();
        UIThread = std::thread(&FLoadingScreen::ThreadMain, this, Instance, std::move(Startup));
        Ready.get();
    }
    catch (...)
    {
        Stop();
        throw;
    }
}

// 종료 이벤트를 전달하고 UI 스레드가 창을 정리할 때까지 기다린다.
void FLoadingScreen::Stop() noexcept
{
    // 호출자는 창을 직접 파괴하지 않고 소유 스레드에 종료를 알린다.
    if (StopEvent) SetEvent(StopEvent);
    if (UIThread.joinable()) UIThread.join();

    if (StopEvent)
    {
        CloseHandle(StopEvent);
        StopEvent = nullptr;
    }
}

// 사용자가 요청한 취소 상태를 스레드 간 안전하게 읽는다.
bool FLoadingScreen::IsCancelRequested() const noexcept
{
    return bCancelRequested.load();
}

// 별도 UI 스레드에서 로딩 창의 전체 수명을 관리한다.
void FLoadingScreen::ThreadMain(HINSTANCE Instance, std::promise<void> Startup)
{
    constexpr wchar_t ClassName[] = L"PepeLoadingScreenWindow";
    HWND Window = nullptr;
    bool bClassRegistered = false;
    bool bStartupReported = false;

    try
    {
        // 실행 파일에 포함된 아이콘을 로고 크기로 읽는다.
        LogoIcon = static_cast<HICON>(LoadImageW(
            Instance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 96, 96, LR_DEFAULTCOLOR));
        if (!LogoIcon)
            throw std::runtime_error("Failed to load PePe loading screen icon.");

        WNDCLASSW WindowClass{};
        WindowClass.lpfnWndProc = &FLoadingScreen::WindowProc;
        WindowClass.hInstance = Instance;
        WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        WindowClass.hIcon = LogoIcon;
        WindowClass.lpszClassName = ClassName;
        if (!RegisterClassW(&WindowClass))
            throw std::runtime_error("Failed to register loading screen window.");
        bClassRegistered = true;

        // 클라이언트 영역을 480×300으로 맞추고 화면 중앙에 배치한다.
        constexpr DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT Bounds{ 0, 0, 480, 300 };
        if (!AdjustWindowRectEx(&Bounds, Style, FALSE, 0))
            throw std::runtime_error("Failed to calculate loading screen size.");

        const int Width = Bounds.right - Bounds.left;
        const int Height = Bounds.bottom - Bounds.top;
        const int X = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
        const int Y = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;
        Window = CreateWindowExW(
            0, ClassName, ApplicationTitle.c_str(), Style, X, Y, Width, Height,
            nullptr, nullptr, Instance, this);
        if (!Window)
            throw std::runtime_error("Failed to create loading screen window.");
        if (!SetTimer(Window, RefreshTimerId, 50, nullptr))
            throw std::runtime_error("Failed to create loading screen refresh timer.");

        ShowWindow(Window, SW_SHOW);
        UpdateWindow(Window);
        Startup.set_value();
        bStartupReported = true;

        // 창 메시지 또는 종료 이벤트가 도착할 때까지 CPU를 소비하지 않고 기다린다.
        bool bRunning = true;
        while (bRunning)
        {
            const DWORD Result = MsgWaitForMultipleObjectsEx(
                1, &StopEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (Result == WAIT_OBJECT_0) break;
            if (Result != WAIT_OBJECT_0 + 1)
            {
                bCancelRequested.store(true);
                break;
            }

            MSG Message{};
            while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
            {
                if (Message.message == WM_QUIT)
                {
                    bCancelRequested.store(true);
                    bRunning = false;
                    break;
                }
                TranslateMessage(&Message);
                DispatchMessageW(&Message);

                // 메시지가 계속 들어와도 종료 요청을 확인한다.
                if (WaitForSingleObject(StopEvent, 0) == WAIT_OBJECT_0)
                {
                    bRunning = false;
                    break;
                }
            }
        }
    }
    catch (...)
    {
        // 창 준비 전의 실패는 Start로 전달하고, 이후 실패는 취소 상태로 알린다.
        if (!bStartupReported) Startup.set_exception(std::current_exception());
        else bCancelRequested.store(true);
    }

    // 모든 Win32 창 자원은 생성한 UI 스레드에서 해제한다.
    if (Window && IsWindow(Window))
    {
        KillTimer(Window, RefreshTimerId);
        DestroyWindow(Window);
    }
    if (bClassRegistered) UnregisterClassW(ClassName, Instance);
    if (LogoIcon)
    {
        DestroyIcon(LogoIcon);
        LogoIcon = nullptr;
    }
}

// 창에 연결된 관리 객체를 찾아 표시와 취소 요청을 처리한다.
LRESULT CALLBACK FLoadingScreen::WindowProc(
    HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    // CreateWindowExW에 전달한 this를 창의 사용자 데이터에 연결한다.
    auto* Self = reinterpret_cast<FLoadingScreen*>(GetWindowLongPtrW(Window, GWLP_USERDATA));
    if (Message == WM_NCCREATE)
    {
        const auto* CreateInfo = reinterpret_cast<const CREATESTRUCTW*>(LParam);
        Self = static_cast<FLoadingScreen*>(CreateInfo->lpCreateParams);
        SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Self));
    }
    if (!Self) return DefWindowProcW(Window, Message, WParam, LParam);

    switch (Message)
    {
    case WM_PAINT:
        try
        {
            Self->Paint(Window);
        }
        catch (...)
        {
            // 상태 복사 등에 실패해도 C++ 예외가 Win32 콜백 밖으로 나가지 않게 한다.
            Self->bCancelRequested.store(true);
            ValidateRect(Window, nullptr);
        }
        return 0;

    case WM_TIMER:
        // 다시 그리기만 예약하고 실제 표시는 WM_PAINT에서 수행한다.
        if (WParam == RefreshTimerId)
        {
            InvalidateRect(Window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        // Paint에서 배경까지 그리므로 기본 배경 지우기는 생략한다.
        return 1;
    case WM_CLOSE:
        // 진행 중인 로딩을 강제 종료하지 않고 메인 스레드에 취소를 요청한다.
        Self->bCancelRequested.store(true);
        InvalidateRect(Window, nullptr, FALSE);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

// DirectX 없이 Win32 GDI로 로고와 안내 문구를 그린다.
// 표시 상태를 복사하여 로고·파일명·진행률을 하나의 화면으로 그린다.
void FLoadingScreen::Paint(HWND Window)
{
    // 문자열 복사와 포맷 작업은 GDI 자원을 얻기 전에 완료한다.
    const FLoadingScreenStatus Status = CopyStatus();
    const bool bCancelled = bCancelRequested.load();
    const std::size_t CompletedCount = (std::min)(
        Status.TotalCount, Status.LoadedCount + Status.SkippedCount + Status.FailedCount);

    FWideString CountText;
    if (Status.bDeterminate)
    {
        CountText = std::format(
            L"완료 {} / {} · 성공 {} · 제외 {} · 실패 {}",
            CompletedCount, Status.TotalCount,
            Status.LoadedCount, Status.SkippedCount, Status.FailedCount);
    }
    else
    {
        CountText = L"잠시만 기다려 주세요.";
    }

    PAINTSTRUCT PaintInfo{};
    HDC WindowDC = BeginPaint(Window, &PaintInfo);
    RECT Client{};
    GetClientRect(Window, &Client);
    const int Width = Client.right - Client.left;
    const int Height = Client.bottom - Client.top;
    if (!WindowDC || Width <= 0 || Height <= 0)
    {
        EndPaint(Window, &PaintInfo);
        return;
    }

    // 메모리 화면에 먼저 그려 주기적 갱신으로 인한 깜빡임을 줄인다.
    HDC MemoryDC = CreateCompatibleDC(WindowDC);
    HBITMAP Bitmap = MemoryDC ? CreateCompatibleBitmap(WindowDC, Width, Height) : nullptr;
    HGDIOBJ PreviousBitmap = Bitmap ? SelectObject(MemoryDC, Bitmap) : nullptr;
    const bool bBuffered = PreviousBitmap && PreviousBitmap != HGDI_ERROR;
    HDC DC = bBuffered ? MemoryDC : WindowDC;

    SetDCBrushColor(DC, RGB(25, 28, 32));
    FillRect(DC, &Client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(DC, TRANSPARENT);
    HGDIOBJ PreviousFont = SelectObject(DC, GetStockObject(DEFAULT_GUI_FONT));

    // 기존 실행 파일의 아이콘과 프로그램 이름을 표시한다.
    constexpr int IconSize = 96;
    DrawIconEx(DC, (Width - IconSize) / 2, 20, LogoIcon,
        IconSize, IconSize, 0, nullptr, DI_NORMAL);

    SetTextColor(DC, RGB(235, 238, 242));
    RECT TitleRect{ 20, 122, Width - 20, 148 };
    DrawTextW(DC, ApplicationTitle.c_str(), -1, &TitleRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 취소 요청은 일반 단계 문구보다 우선 표시한다.
    const wchar_t* StageText = bCancelled
        ? L"취소 요청 중입니다. 현재 작업이 끝날 때까지 기다려 주세요."
        : Status.Stage.c_str();
    SetTextColor(DC, RGB(190, 200, 210));
    RECT StageRect{ 16, 154, Width - 16, 178 };
    DrawTextW(DC, StageText, -1, &StageRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetTextColor(DC, RGB(155, 170, 185));
    RECT FileRect{ 24, 184, Width - 24, 206 };
    DrawTextW(DC, Status.FileName.c_str(), -1, &FileRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 전체 개수가 확정되면 실제 비율을, 아직 모르면 이동하는 막대를 표시한다.
    RECT Track{ 32, 220, Width - 32, 234 };
    const int TrackWidth = (std::max)(0, static_cast<int>(Track.right - Track.left));
    if (TrackWidth > 0)
    {
        SetDCBrushColor(DC, RGB(48, 55, 63));
        FillRect(DC, &Track, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

        RECT Fill = Track;
        if (Status.bDeterminate)
        {
            // 대상이 0개이면 처리할 파일이 없는 완료 상태로 표현한다.
            const double Ratio = Status.TotalCount > 0
                ? static_cast<double>(CompletedCount) / static_cast<double>(Status.TotalCount)
                : 1.0;
            Fill.right = Fill.left + static_cast<int>(TrackWidth * Ratio);
        }
        else if (!bCancelled)
        {
            // 시작 시각을 공유하지 않고 UI 스레드의 현재 시각으로 움직임을 계산한다.
            const int SegmentWidth = (std::min)(72, TrackWidth);
            const int Offset = static_cast<int>(
                (GetTickCount64() / 10) % static_cast<ULONGLONG>(TrackWidth + SegmentWidth));
            const int SegmentLeft = Track.left + Offset - SegmentWidth;
            Fill.left = (std::max)(Track.left, static_cast<LONG>(SegmentLeft));
            Fill.right = (std::min)(Track.right, static_cast<LONG>(SegmentLeft + SegmentWidth));
        }
        else
        {
            Fill.right = Fill.left;
        }

        if (Fill.right > Fill.left)
        {
            SetDCBrushColor(DC, RGB(105, 190, 120));
            FillRect(DC, &Fill, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
    }

    // 처리 개수는 성공뿐 아니라 제외·실패까지 포함하여 표시한다.
    SetTextColor(DC, RGB(165, 175, 188));
    RECT CountRect{ 16, 244, Width - 16, 276 };
    DrawTextW(DC, CountText.c_str(), -1, &CountRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 완성된 화면을 복사하고 생성한 GDI 자원을 모두 정리한다.
    if (PreviousFont && PreviousFont != HGDI_ERROR) SelectObject(DC, PreviousFont);
    if (bBuffered)
    {
        BitBlt(WindowDC, 0, 0, Width, Height, MemoryDC, 0, 0, SRCCOPY);
        SelectObject(MemoryDC, PreviousBitmap);
    }
    if (Bitmap) DeleteObject(Bitmap);
    if (MemoryDC) DeleteDC(MemoryDC);
    EndPaint(Window, &PaintInfo);
}

// 표시 상태를 한 번에 교체하여 문자열과 개수가 서로 다른 시점으로 섞이지 않게 한다.
void FLoadingScreen::SetStatus(FLoadingScreenStatus InStatus)
{
    // 전달받은 상태의 소유권을 이동하고 즉시 잠금을 해제한다.
    std::lock_guard<std::mutex> Lock(StatusMutex);
    CurrentStatus = std::move(InStatus);
}

// 현재 상태를 복사하고 실제 그리기는 잠금을 해제한 뒤 수행한다.
FLoadingScreenStatus FLoadingScreen::CopyStatus()
{
    // 한 프레임의 모든 표시 값이 같은 상태를 사용하도록 복사한다.
    std::lock_guard<std::mutex> Lock(StatusMutex);
    return CurrentStatus;
}