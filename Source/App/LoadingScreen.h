#pragma once
#include <Windows.h>
#include <atomic>
#include <future>
#include <thread>
#include "Core/Container/String.h"
#include <cstddef>
#include <mutex>

// 로딩 창에 표시할 값만 보관하며 엔진 객체나 리소스를 참조하지 않는다.
struct FLoadingScreenStatus
{
    FWideString Stage = L"Loading...";
    FWideString FileName;

    std::size_t TotalCount = 0;
    std::size_t LoadedCount = 0;
    std::size_t SkippedCount = 0;
    std::size_t FailedCount = 0;

    // 전체 개수가 확정되었을 때만 실제 진행률을 표시한다.
    bool bDeterminate = false;
};

class FLoadingScreen
{
public:
    // 로딩 창 관리 객체를 생성한다.
    FLoadingScreen() = default;

    // 소멸 전에 UI 스레드와 종료 이벤트를 정리한다.
    ~FLoadingScreen();

    // 창과 스레드를 소유하므로 복사를 금지한다.
    FLoadingScreen(const FLoadingScreen&) = delete;
    FLoadingScreen& operator=(const FLoadingScreen&) = delete;

    // UI 스레드를 시작하고 로고 창의 생성 결과를 기다린다.
    void Start(HINSTANCE Instance, FWideStringView Title);

    // UI 스레드에 종료를 요청하고 정리가 끝날 때까지 기다린다.
    void Stop() noexcept;

    // 사용자가 로딩 창 닫기를 눌렀는지 확인한다.
    bool IsCancelRequested() const noexcept;

    // 메인 스레드가 전달한 표시 상태를 안전하게 교체한다.
    void SetStatus(FLoadingScreenStatus InStatus);
private:
    // 별도 스레드에서 창 생성과 메시지 처리를 수행한다.
    void ThreadMain(HINSTANCE Instance, std::promise<void> Startup);

    // 로딩 창의 표시와 닫기 메시지를 처리한다.
    static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);

    // 배경과 프로그램 아이콘, 안내 문구를 그린다.
    void Paint(HWND Window);

    std::thread UIThread;
    HANDLE StopEvent = nullptr;
    std::atomic<bool> bCancelRequested{ false };

    // 제목은 시작 전에 설정하고, 실행 중에는 변경하지 않는다.
    FWideString ApplicationTitle;

    // 아이콘 생성·사용·해제는 UI 스레드에서만 수행한다.
    HICON LogoIcon = nullptr;

    // UI 스레드에서 한 번의 그리기에 사용할 상태 복사본을 얻는다.
    FLoadingScreenStatus CopyStatus();

    static constexpr UINT_PTR RefreshTimerId = 1;
    std::mutex StatusMutex;
    FLoadingScreenStatus CurrentStatus;
};